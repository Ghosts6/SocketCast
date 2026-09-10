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
  const out = new Uint8Array(11 + sps.length + pps.length);
  let o = 0;
  out[o++] = 1;
  out[o++] = sps[1];
  out[o++] = sps[2];
  out[o++] = sps[3];
  out[o++] = 0xff;
  out[o++] = 0xe1;
  out[o++] = (sps.length >> 8) & 0xff;
  out[o++] = sps.length & 0xff;
  out.set(sps, o);
  o += sps.length;
  out[o++] = 1;
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
) {
  const frameCountRef = useRef(0);
  const bytesRef = useRef(0);

  useEffect(() => {
    if (!connected || !sessionId || !canvasRef.current) return;

    const canvas = canvasRef.current;
    let websocket: WebSocket | null = null;
    let decoder: VideoDecoder | null = null;
    let configured = false;
    let sps: Uint8Array | null = null;
    let pps: Uint8Array | null = null;
    let needKey = true;
    let timestampUs = 0;
    let cancelled = false;
    let pingTimer: number | undefined;
    let statusTimer: number | undefined;

    const closeDecoder = () => {
      try {
        decoder?.close();
      } catch {
        /* ignore */
      }
      decoder = null;
      configured = false;
      needKey = true;
    };

    const ensureDecoder = () => {
      if (!sps || !pps || typeof VideoDecoder === "undefined") return;
      if (configured && decoder && decoder.state === "configured") return;
      closeDecoder();
      try {
        decoder = new VideoDecoder({
          output: (frame) => {
            if (cancelled || !canvasRef.current) {
              frame.close();
              return;
            }
            const ctx = canvasRef.current.getContext("2d");
            if (ctx) {
              ctx.drawImage(frame, 0, 0, canvasRef.current.width, canvasRef.current.height);
            }
            frame.close();
            frameCountRef.current += 1;
          },
          error: (err) => {
            console.error("VideoDecoder error:", err);
            drawStatus(canvas, "Decoder error — receiving bytes", bytesRef.current);
            closeDecoder();
          },
        });
        const codec = codecFromSps(sps);
        decoder.configure({
          codec,
          description: buildAvcC(sps, pps),
          optimizeForLatency: true,
          hardwareAcceleration: "prefer-software",
        });
        configured = true;
        needKey = true;
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

        const data = new Uint8Array(event.data as ArrayBuffer);
        bytesRef.current += data.byteLength;
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
        if (type === 5) needKey = false;

        timestampUs += 33_333;
        try {
          decoder.decode(
            new EncodedVideoChunk({
              type: type === 5 ? "key" : "delta",
              timestamp: timestampUs,
              data: toLengthPrefixed(nal),
            }),
          );
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
      websocket?.close();
      closeDecoder();
    };
  }, [connected, sessionId, canvasRef]);

  return { frameCount: frameCountRef };
}
