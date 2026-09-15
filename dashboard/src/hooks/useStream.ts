import { useEffect, useRef } from "react";

const WS_BASE = import.meta.env.VITE_WS_BASE || "ws://localhost:8000/ws";

function nalType(data: Uint8Array): number | null {
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

function stripStartCode(data: Uint8Array): Uint8Array {
  if (data.length >= 4 && data[0] === 0 && data[1] === 0 && data[2] === 0 && data[3] === 1) {
    return data.subarray(4);
  }
  if (data.length >= 3 && data[0] === 0 && data[1] === 0 && data[2] === 1) {
    return data.subarray(3);
  }
  return data;
}

function toLengthPrefixed(nal: Uint8Array): Uint8Array {
  const out = new Uint8Array(4 + nal.length);
  const len = nal.length;
  out[0] = (len >>> 24) & 0xff;
  out[1] = (len >>> 16) & 0xff;
  out[2] = (len >>> 8) & 0xff;
  out[3] = len & 0xff;
  out.set(nal, 4);
  return out;
}

function buildAvcC(sps: Uint8Array, pps: Uint8Array): Uint8Array {
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

function codecFromSps(sps: Uint8Array): string {
  const profile = sps[1].toString(16).padStart(2, "0");
  const compat = sps[2].toString(16).padStart(2, "0");
  const level = sps[3].toString(16).padStart(2, "0");
  return `avc1.${profile}${compat}${level}`;
}

// Wire format: [1B type][8B pts_us big-endian][payload].
const kTypeVideo = 0x00;
const kTypeAudio = 0x01;
const kFrameHeaderLen = 9;

function readPtsUs(data: Uint8Array): number {
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
    let lastKeyframePtsUs = -1;
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

    let debugFirstVideoLogged = false;
    let debugMsgCount = 0;
    let debugDecodeCount = 0;
    const drawFrame = (frame: VideoFrame) => {
      if (canvasRef.current) {
        const ctx = canvasRef.current.getContext("2d");
        if (ctx) {
          ctx.drawImage(frame, 0, 0, canvasRef.current.width, canvasRef.current.height);
        }
        if (!debugFirstVideoLogged) {
          debugFirstVideoLogged = true;
          console.log(`[SYNC] first video frame drawn at perf=${performance.now().toFixed(1)}ms, pts=${(frame.timestamp / 1000).toFixed(1)}ms`);
        }
        frameCountRef.current += 1;
      }
      frame.close();
    };

    let debugLastRenderLog = 0;
    const renderLoop = () => {
      if (cancelled) return;
      const nowMs = performance.now();

      if (nowMs - debugLastRenderLog > 1000) {
        debugLastRenderLog = nowMs;
        console.log(`[RENDER] tick nowMs=${nowMs.toFixed(0)} queueLen=${renderQueue.length} anchorPerf=${renderAnchorPerfMs?.toFixed(0)} anchorPts=${renderAnchorPtsMs?.toFixed(0)} frontPts=${renderQueue[0] ? (renderQueue[0].timestamp / 1000).toFixed(0) : "none"}`);
      }

      if (renderAnchorPerfMs == null && renderQueue.length > 0) {
        renderAnchorPerfMs = nowMs;
        renderAnchorPtsMs = renderQueue[0].timestamp / 1000;
        console.log(`[RENDER] anchor SET perf=${renderAnchorPerfMs.toFixed(0)} pts=${renderAnchorPtsMs.toFixed(0)}`);
      }

      while (renderQueue.length > kMaxQueuedFrames) {
        renderQueue.shift()!.close();
      }

      while (renderQueue.length > 0) {
        const framePtsMs = renderQueue[0].timestamp / 1000;
        let dueAt = renderAnchorPerfMs! + (framePtsMs - renderAnchorPtsMs!);

        // A pts far ahead of schedule (e.g. jumping from a resent stale
        // keyframe to the live buffer's position) would idle out the gap —
        // resync to "now" instead, the gap was never real playback time.
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

      rafHandle = requestAnimationFrame(renderLoop);
    };
    rafHandle = requestAnimationFrame(renderLoop);

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
      lastKeyframePtsUs = -1;
      while (renderQueue.length > 0) {
        renderQueue.shift()!.close();
      }
      renderAnchorPerfMs = null;
      renderAnchorPtsMs = null;
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

    let debugFirstAudioLogged = false;
    const scheduleAudio = (audioData: AudioData) => {
      if (!audioCtx || !gainNode) {
        audioData.close();
        return;
      }
      try {
        const { numberOfChannels, numberOfFrames, sampleRate } = audioData;
        const buffer = audioCtx.createBuffer(numberOfChannels, numberOfFrames, sampleRate);
        const channelData = new Float32Array(numberOfFrames);
        for (let ch = 0; ch < numberOfChannels; ch++) {
          audioData.copyTo(channelData, { planeIndex: ch, format: "f32-planar" });
          buffer.copyToChannel(channelData, ch);
        }
        const source = audioCtx.createBufferSource();
        source.buffer = buffer;
        source.connect(gainNode);
        const startAt = Math.max(audioCtx.currentTime, nextAudioTime);
        if (!debugFirstAudioLogged) {
          debugFirstAudioLogged = true;
          console.log(`[SYNC] first audio chunk scheduled at perf=${performance.now().toFixed(1)}ms, pts=${(audioData.timestamp / 1000).toFixed(1)}ms, audioCtx.currentTime=${audioCtx.currentTime.toFixed(3)}s, startAt=${startAt.toFixed(3)}s, audioCtx.state=${audioCtx.state}`);
        }
        source.start(startAt);
        nextAudioTime = startAt + buffer.duration;
      } catch (err) {
        console.error("audio schedule failed:", err);
      } finally {
        audioData.close();
      }
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
        console.log(`[SYNC] AudioContext created at perf=${performance.now().toFixed(1)}ms, state=${audioCtx.state}`);
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
            if (debugDecodeCount <= 12) {
              console.log(`[RENDER] push perf=${performance.now().toFixed(0)} pts=${(frame.timestamp / 1000).toFixed(0)} newQueueLen=${renderQueue.length + 1}`);
            }
            renderQueue.push(frame);
          },
          error: (err) => {
            console.error(`VideoDecoder error (after ${debugDecodeCount} decode() calls, highestDecodedPtsUs=${highestDecodedPtsUs}):`, err);
            drawStatus(canvas, "Decoder error — receiving bytes", bytesRef.current);
            closeDecoder();
          },
        });
        const codec = codecFromSps(sps);
        const avcC = buildAvcC(sps, pps);
        console.log(`[NAL] configuring decoder: codec=${codec} sps_len=${sps.length} pps_len=${pps.length} sps_hex=${Array.from(sps).map((b) => b.toString(16).padStart(2, "0")).join(" ")} pps_hex=${Array.from(pps).map((b) => b.toString(16).padStart(2, "0")).join(" ")}`);

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
        console.log(`[SYNC] VideoDecoder configured at perf=${performance.now().toFixed(1)}ms`);
        drawStatus(canvas, "Decoder ready — waiting for keyframe", bytesRef.current);
      } catch (err) {
        console.error("Failed to configure VideoDecoder:", err);
        closeDecoder();
      }
    };

    drawStatus(canvas, "Connecting to stream…");

    try {
      websocket = new WebSocket(`${WS_BASE}/stream/${sessionId}`);
      websocket.binaryType = "arraybuffer";

      websocket.onopen = () => {
        console.log(`[SYNC] WS opened at perf=${performance.now().toFixed(1)}ms`);
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
          if (audioDecoder && audioDecoder.state === "configured") {
            try {
              audioDecoder.decode(
                new EncodedAudioChunk({ type: "key", timestamp: ptsUs, data }),
              );
            } catch (err) {
              console.error("audio decode failed:", err);
            }
          }
          return;
        }
        if (msgType !== kTypeVideo) return;

        const type = nalType(data);
        if (type == null) return;

        const nal = stripStartCode(data);

        if (debugMsgCount < 25) {
          debugMsgCount++;
          const hex = Array.from(nal.slice(0, 10)).map((b) => b.toString(16).padStart(2, "0")).join(" ");
          console.log(`[NAL] #${debugMsgCount} type=${type} pts=${ptsUs} len=${nal.length} bytes=${hex}`);
        }

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

        // A P-frame this far past the last keyframe we decoded almost
        // certainly references frames we never saw — decode would "succeed"
        // but produce macroblocking. Bail back to waiting for a real keyframe.
        const kMaxGopGapUs = 1_000_000;
        if (type !== 5 && ptsUs - lastKeyframePtsUs > kMaxGopGapUs) {
          needKey = true;
          return;
        }
        if (type === 5) {
          needKey = false;
          lastKeyframePtsUs = ptsUs;
        }

        try {
          // avcC declares 4-byte length-prefixed NALs, not Annex-B start codes.
          const chunk = new EncodedVideoChunk({
            type: type === 5 ? "key" : "delta",
            timestamp: ptsUs,
            data: toLengthPrefixed(nal),
          });
          if (debugDecodeCount < 10) {
            debugDecodeCount++;
            console.log(`[NAL] decode() call #${debugDecodeCount}: type=${type === 5 ? "key" : "delta"} pts=${ptsUs} len=${nal.length} decodeQueueSize=${decoder.decodeQueueSize}`);
          }
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
