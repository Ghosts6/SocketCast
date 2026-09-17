import { useEffect, useRef } from "react";

const WS_BASE = import.meta.env.VITE_WS_BASE || "ws://localhost:8000/ws";

export function nalType(data: Uint8Array): number | null {
  let i = 0;
  if (data.length >= 4 && data[0] === 0 && data[1] === 0 && data[2] === 0 && data[3] === 1) {
    i = 4;
  } else if (data.length >= 3 && data[0] === 0 && data[1] === 0 && data[2] === 1) {
    i = 3;
  } else {
    return null;
  }
  if (i >= data.length) return null;
  return data[i] & 0x1f;
}

export function stripStartCode(data: Uint8Array): Uint8Array {
  if (data.length >= 4 && data[0] === 0 && data[1] === 0 && data[2] === 0 && data[3] === 1) {
    return data.subarray(4);
  }
  if (data.length >= 3 && data[0] === 0 && data[1] === 0 && data[2] === 1) {
    return data.subarray(3);
  }
  return data;
}

export function toLengthPrefixed(nal: Uint8Array): Uint8Array {
  const out = new Uint8Array(4 + nal.length);
  const len = nal.length;
  out[0] = (len >>> 24) & 0xff;
  out[1] = (len >>> 16) & 0xff;
  out[2] = (len >>> 8) & 0xff;
  out[3] = len & 0xff;
  out.set(nal, 4);
  return out;
}

export function buildAvcC(sps: Uint8Array, pps: Uint8Array): Uint8Array {
  // avcC box (ISO/IEC 14496-15): SPS/PPS entries store the complete NAL unit,
  // header byte included — not just the RBSP.
  const out = new Uint8Array(11 + sps.length + pps.length);
  let o = 0;
  out[o++] = 1;  // version
  out[o++] = sps[1];  // profile_idc
  out[o++] = sps[2];  // profile_compatibility
  out[o++] = sps[3];  // level_idc
  out[o++] = 0xff;  // reserved(6 bits) + nalUnitLengthField (2 bits) = 0xFF for 4-byte lengths
  out[o++] = 0xe1;  // reserved(3 bits) + numOfSequenceParameterSets (5 bits) = 0xE1 for 1 SPS
  out[o++] = (sps.length >> 8) & 0xff;
  out[o++] = sps.length & 0xff;
  out.set(sps, o);
  o += sps.length;
  out[o++] = 1;  // numOfPictureParameterSets
  out[o++] = (pps.length >> 8) & 0xff;
  out[o++] = pps.length & 0xff;
  out.set(pps, o);
  return out;
}

export function codecFromSps(sps: Uint8Array): string {
  const profile = sps[1].toString(16).padStart(2, "0");
  const compat = sps[2].toString(16).padStart(2, "0");
  const level = sps[3].toString(16).padStart(2, "0");
  return `avc1.${profile}${compat}${level}`;
}

// Wire format: [1B type][8B pts_us big-endian][payload].
export const kTypeVideo = 0x00;
export const kTypeAudio = 0x01;
export const kFrameHeaderLen = 9;

export function readPtsUs(data: Uint8Array): number {
  const view = new DataView(data.buffer, data.byteOffset + 1, 8);
  return Number(view.getBigUint64(0, false));
}

function drawStatus(canvas: HTMLCanvasElement, text: string, bytes?: number) {
  const ctx = canvas.getContext("2d");
  if (!ctx) return;
  ctx.fillStyle = "#111";
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = "#9ca3af";
  ctx.font = "20px sans-serif";
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  ctx.fillText(text, canvas.width / 2, canvas.height / 2 - 12);
  if (bytes != null) {
    ctx.fillStyle = "#6ee7b7";
    ctx.font = "14px monospace";
    ctx.fillText(`${(bytes / 1024).toFixed(1)} KB received`, canvas.width / 2, canvas.height / 2 + 18);
  }
}

export function useStream(
  canvasRef: React.RefObject<HTMLCanvasElement | null>,
  connected: boolean,
  sessionId?: string,
  muted: boolean = false,
) {
  const frameCountRef = useRef(0);
  const bytesRef = useRef(0);
  const gainNodeRef = useRef<GainNode | null>(null);
  const mutedRef = useRef(muted);

  useEffect(() => {
    mutedRef.current = muted;
    if (gainNodeRef.current) {
      gainNodeRef.current.gain.value = muted ? 0 : 1;
    }
  }, [muted]);

  useEffect(() => {
    if (!connected || !sessionId || !canvasRef.current) return;

    const canvas = canvasRef.current;
    let websocket: WebSocket | null = null;
    let decoder: VideoDecoder | null = null;
    let configured = false;
    let sps: Uint8Array | null = null;
    let pps: Uint8Array | null = null;
    let needKey = true;
    // pts is strictly increasing from the source; anything at or before what
    // we've already decoded is a stale resend (see the engine's keyframe
    // resend below), not new content — splicing it back in would break the
    // H.264 reference chain and cause visible macroblocking.
    let highestDecodedPtsUs = -1;
    let cancelled = false;
    let pingTimer: number | undefined;
    let statusTimer: number | undefined;

    let audioCtx: AudioContext | null = null;
    let gainNode: GainNode | null = null;
    let audioDecoder: AudioDecoder | null = null;
    let audioConfigured = false;
    let nextAudioTime = 0;

    // Decode bursts arrive faster than they should play; queue frames and
    // pace each off an anchored pts->wall-clock offset instead of decode order.
    const renderQueue: VideoFrame[] = [];
    let renderAnchorPerfMs: number | null = null;
    let renderAnchorPtsMs: number | null = null;
    let rafHandle: number | null = null;
    const kMaxQueuedFrames = 60;
    // Watchdog: force-draw the front frame if nothing's painted in this long,
    // rather than trusting the pts schedule below.
    let lastDrawPerfMs: number | null = performance.now();
    const kMaxStallMs = 1500;

    // Engine/bridge bulk-drains the whole AudioBuffer on first poll — keep
    // encoded packets and only decode/schedule near the video playhead so we
    // can PTS-align instead of chaining ASAP on a suspended AudioContext.
    const audioPacketQueue: { ptsUs: number; data: Uint8Array }[] = [];
    const kAudioScheduleAheadSec = 0.75;
    const kAudioLateSlackSec = 0.05;
    // Must hold a whole clip until the video clock exists and pumpAudio can
    // trim to the playhead. A small drop-oldest cap (e.g. 100) kept only the
    // *end* of the song while video started at pts≈0 → permanent silence.

    const drawFrame = (frame: VideoFrame) => {
      if (canvasRef.current) {
        const ctx = canvasRef.current.getContext("2d");
        if (ctx) {
          ctx.drawImage(frame, 0, 0, canvasRef.current.width, canvasRef.current.height);
        }
        frameCountRef.current += 1;
      }
      frame.close();
      lastDrawPerfMs = performance.now();
    };

    const videoMediaSec = (): number | null => {
      if (renderAnchorPerfMs == null || renderAnchorPtsMs == null) return null;
      return (renderAnchorPtsMs + (performance.now() - renderAnchorPerfMs)) / 1000;
    };

    const scheduleAudio = (audioData: AudioData) => {
      if (!audioCtx || !gainNode) {
        audioData.close();
        return;
      }
      const vMedia = videoMediaSec();
      if (vMedia == null || audioCtx.state !== "running") {
        audioData.close();
        return;
      }
      try {
        const { numberOfChannels, numberOfFrames, sampleRate } = audioData;
        const ptsSec = audioData.timestamp / 1_000_000;
        if (ptsSec < vMedia - kAudioLateSlackSec) {
          audioData.close();
          return;
        }
        const buffer = audioCtx.createBuffer(numberOfChannels, numberOfFrames, sampleRate);
        const channelData = new Float32Array(numberOfFrames);
        for (let ch = 0; ch < numberOfChannels; ch++) {
          audioData.copyTo(channelData, { planeIndex: ch, format: "f32-planar" });
          buffer.copyToChannel(channelData, ch);
        }
        const source = audioCtx.createBufferSource();
        source.buffer = buffer;
        source.connect(gainNode);
        // Align this chunk's media pts to the same clock the video render loop uses.
        const startAt = Math.max(
          audioCtx.currentTime + 0.01,
          audioCtx.currentTime + (ptsSec - vMedia),
          nextAudioTime,
        );
        source.start(startAt);
        nextAudioTime = startAt + buffer.duration;
      } catch (err) {
        console.error("audio schedule failed:", err);
      } finally {
        audioData.close();
      }
    };

    const pumpAudio = () => {
      if (!audioDecoder || audioDecoder.state !== "configured" || !audioCtx) return;
      if (audioCtx.state === "suspended") {
        audioCtx.resume().catch(() => {});
        return;
      }
      if (audioCtx.state !== "running") return;
      const vMedia = videoMediaSec();
      if (vMedia == null) return;

      while (audioPacketQueue.length > 0) {
        const pkt = audioPacketQueue[0];
        const ptsSec = pkt.ptsUs / 1_000_000;
        if (ptsSec < vMedia - kAudioLateSlackSec) {
          audioPacketQueue.shift();
          continue;
        }
        if (ptsSec > vMedia + kAudioScheduleAheadSec) break;
        audioPacketQueue.shift();
        try {
          audioDecoder.decode(
            new EncodedAudioChunk({ type: "key", timestamp: pkt.ptsUs, data: pkt.data }),
          );
        } catch (err) {
          console.error("audio decode failed:", err);
        }
      }
    };

    const renderLoop = () => {
      if (cancelled) return;
      // A throw here must not kill the rAF chain — that was the freeze bug:
      // one bad tick, then the canvas never updates again.
      try {
        const nowMs = performance.now();

        if (renderAnchorPerfMs == null && renderQueue.length > 0) {
          renderAnchorPerfMs = nowMs;
          renderAnchorPtsMs = renderQueue[0].timestamp / 1000;
        }

        while (renderQueue.length > kMaxQueuedFrames) {
          renderQueue.shift()!.close();
        }

        // Stalled past kMaxStallMs — stop trusting the anchor/pts math,
        // force-draw the front frame and resync.
        if (
          renderQueue.length > 0 &&
          lastDrawPerfMs != null &&
          nowMs - lastDrawPerfMs > kMaxStallMs
        ) {
          const frame = renderQueue.shift()!;
          renderAnchorPerfMs = nowMs;
          renderAnchorPtsMs = frame.timestamp / 1000;
          drawFrame(frame);
        }

        while (renderQueue.length > 0) {
          const framePtsMs = renderQueue[0].timestamp / 1000;
          let dueAt = renderAnchorPerfMs! + (framePtsMs - renderAnchorPtsMs!);

          // A pts far ahead of schedule (e.g. a resent stale keyframe jumping
          // to the live position) would idle out a gap that was never real playback time.
          const kMaxScheduleAheadMs = 1000;
          if (dueAt > nowMs + kMaxScheduleAheadMs) {
            renderAnchorPerfMs = nowMs;
            renderAnchorPtsMs = framePtsMs;
            dueAt = nowMs;
          }

          if (nowMs < dueAt) break;

          const frame = renderQueue.shift()!;
          // Drop instead of paint if a newer frame is also due — avoids a
          // fast-forward blur of instantly-drawn frames after any hiccup.
          if (renderQueue.length > 0) {
            const nextPtsMs = renderQueue[0].timestamp / 1000;
            if (renderAnchorPerfMs! + (nextPtsMs - renderAnchorPtsMs!) <= nowMs) {
              frame.close();
              continue;
            }
          }
          drawFrame(frame);
        }

        pumpAudio();
      } catch (err) {
        console.error("renderLoop error (recovering, not stalling):", err);
      } finally {
        if (!cancelled) {
          rafHandle = requestAnimationFrame(renderLoop);
        }
      }
    };

    const closeDecoder = () => {
      try {
        decoder?.close();
      } catch {
        /* ignore */
      }
      decoder = null;
      configured = false;
      needKey = true;
      highestDecodedPtsUs = -1;
      while (renderQueue.length > 0) {
        renderQueue.shift()!.close();
      }
      renderAnchorPerfMs = null;
      renderAnchorPtsMs = null;
      lastDrawPerfMs = performance.now();
    };

    const closeAudioDecoder = () => {
      try {
        audioDecoder?.close();
      } catch {
        /* ignore */
      }
      audioDecoder = null;
      audioConfigured = false;
    };

    const ensureAudioDecoder = () => {
      if (typeof AudioDecoder === "undefined") return;
      if (audioConfigured && audioDecoder && audioDecoder.state === "configured") return;
      const AudioCtxCtor =
        window.AudioContext || (window as unknown as { webkitAudioContext?: typeof AudioContext })
          .webkitAudioContext;
      if (!AudioCtxCtor) return;
      if (!audioCtx) {
        audioCtx = new AudioCtxCtor();
        gainNode = audioCtx.createGain();
        gainNode.gain.value = mutedRef.current ? 0 : 1;
        gainNode.connect(audioCtx.destination);
        gainNodeRef.current = gainNode;
        nextAudioTime = 0;
      }
      if (audioCtx.state === "suspended") {
        audioCtx.resume().catch(() => {
          /* autoplay-gated; will retry on next frame */
        });
      }
      closeAudioDecoder();
      try {
        audioDecoder = new AudioDecoder({
          output: (audioData) => {
            if (cancelled) {
              audioData.close();
              return;
            }
            scheduleAudio(audioData);
          },
          error: (err) => {
            console.error("AudioDecoder error:", err);
            closeAudioDecoder();
          },
        });
        // ADTS is self-describing; these are just initial hints TS requires.
        audioDecoder.configure({ codec: "mp4a.40.2", sampleRate: 48000, numberOfChannels: 2 });
        audioConfigured = true;
      } catch (err) {
        console.error("Failed to configure AudioDecoder:", err);
        closeAudioDecoder();
      }
    };

    const ensureDecoder = () => {
      if (!sps || !pps || typeof VideoDecoder === "undefined") return;
      if (configured && decoder && decoder.state === "configured") return;
      closeDecoder();
      try {
        decoder = new VideoDecoder({
          output: (frame) => {
            if (cancelled) {
              frame.close();
              return;
            }
            renderQueue.push(frame);
          },
          error: (err) => {
            console.error("VideoDecoder error:", err);
            drawStatus(canvas, "Decoder error — receiving bytes", bytesRef.current);
            closeDecoder();
          },
        });
        const codec = codecFromSps(sps);
        const avcC = buildAvcC(sps, pps);

        try {
          decoder.configure({
            codec,
            description: avcC,
            optimizeForLatency: true,
            hardwareAcceleration: "prefer-software",
          });
        } catch (descErr) {
          console.warn("Failed with avcC, trying without description:", descErr);
          decoder.configure({
            codec,
            optimizeForLatency: true,
            hardwareAcceleration: "prefer-software",
          });
        }

        configured = true;
        needKey = true;
        drawStatus(canvas, "Decoder ready — waiting for keyframe", bytesRef.current);
      } catch (err) {
        console.error("Failed to configure VideoDecoder:", err);
        closeDecoder();
      }
    };

    rafHandle = requestAnimationFrame(renderLoop);
    drawStatus(canvas, "Connecting to stream…");

    try {
      websocket = new WebSocket(`${WS_BASE}/stream/${sessionId}`);
      websocket.binaryType = "arraybuffer";

      websocket.onopen = () => {
        drawStatus(canvas, "Waiting for H.264…");
        pingTimer = window.setInterval(() => {
          if (websocket?.readyState === WebSocket.OPEN) websocket.send("ping");
        }, 5000);
        statusTimer = window.setInterval(() => {
          if (!configured || frameCountRef.current === 0) {
            drawStatus(
              canvas,
              configured ? "Waiting for keyframe…" : "Receiving H.264…",
              bytesRef.current,
            );
          }
        }, 500);
      };

      websocket.onmessage = (event) => {
        if (typeof event.data === "string") {
          try {
            const msg = JSON.parse(event.data);
            if (msg.type === "frame" && msg.status === "ready") {
              drawStatus(canvas, "Stream ready — waiting for media");
            }
          } catch {
            /* ignore */
          }
          return;
        }

        const raw = new Uint8Array(event.data as ArrayBuffer);
        bytesRef.current += raw.byteLength;
        if (raw.length <= kFrameHeaderLen) return;

        const msgType = raw[0];
        const ptsUs = readPtsUs(raw);
        const data = raw.subarray(kFrameHeaderLen);

        if (msgType === kTypeAudio) {
          // Confirm the payload is really an ADTS frame (sync word 0xFFF) before decoding.
          if (data.length < 2 || data[0] !== 0xff || (data[1] & 0xf0) !== 0xf0) return;
          ensureAudioDecoder();
          // Copy — the WS buffer is reused. Queue until the video clock exists,
          // then pump near the playhead (see pumpAudio).
          audioPacketQueue.push({ ptsUs, data: data.slice() });
          pumpAudio();
          return;
        }
        if (msgType !== kTypeVideo) return;

        const type = nalType(data);
        if (type == null) return;

        const nal = stripStartCode(data);

        if (type === 7) {
          sps = nal.slice();
          ensureDecoder();
          return;
        }
        if (type === 8) {
          pps = nal.slice();
          ensureDecoder();
          return;
        }

        // Ignore non-VCL (SEI, AUD, etc.)
        if (type !== 1 && type !== 5) return;

        if (!configured || !decoder || decoder.state !== "configured") {
          return;
        }

        if (needKey && type !== 5) {
          return;
        }
        if (ptsUs <= highestDecodedPtsUs) return;

        // Detect a *timeline discontinuity* (missing NALs / stale cached keyframe
        // jumped to live). Even one skipped reference frame causes macroblocking.
        // ~100ms ≈ 3 frames at 30fps — tolerates jitter, rejects real gaps.
        // (Do NOT measure against last keyframe: real GOPs are multi-second.)
        const kMaxDecodeGapUs = 100_000;
        if (
          type !== 5 &&
          highestDecodedPtsUs >= 0 &&
          ptsUs - highestDecodedPtsUs > kMaxDecodeGapUs
        ) {
          needKey = true;
          return;
        }
        if (type === 5) {
          needKey = false;
        }

        try {
          // avcC declares 4-byte length-prefixed NALs, not Annex-B start codes.
          const chunk = new EncodedVideoChunk({
            type: type === 5 ? "key" : "delta",
            timestamp: ptsUs,
            data: toLengthPrefixed(nal),
          });
          decoder.decode(chunk);
          highestDecodedPtsUs = ptsUs;
        } catch (err) {
          console.error("decode failed:", err);
        }
      };

      websocket.onerror = () => {
        drawStatus(canvas, "Stream WebSocket error");
      };
    } catch (err) {
      console.error("Stream WebSocket connection error:", err);
      drawStatus(canvas, "Failed to connect stream WS");
    }

    return () => {
      cancelled = true;
      if (pingTimer != null) window.clearInterval(pingTimer);
      if (statusTimer != null) window.clearInterval(statusTimer);
      if (rafHandle != null) cancelAnimationFrame(rafHandle);
      websocket?.close();
      closeDecoder();
      closeAudioDecoder();
      audioPacketQueue.length = 0;
      try {
        audioCtx?.close();
      } catch {
        /* ignore */
      }
      audioCtx = null;
      gainNode = null;
      gainNodeRef.current = null;
    };
  }, [connected, sessionId, canvasRef]);

  return { frameCount: frameCountRef };
}
