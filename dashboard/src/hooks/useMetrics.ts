import { useState, useEffect, useCallback } from "react";
import type { StreamMetrics } from "../types";

const WS_BASE = import.meta.env.VITE_WS_BASE || "ws://localhost:8000/ws";

const DEFAULT_METRICS: StreamMetrics = {
  framesReceived: 0,
  packetsReceived: 0,
  bytesReceived: 0,
  currentBitrateMbps: 0,
  jitterMs: 0,
  lossPercent: 0,
  rttMs: 0,
  streamActive: false,
};

export function useMetrics(connected: boolean, sessionId?: string) {
  const [metrics, setMetrics] = useState<StreamMetrics>(DEFAULT_METRICS);
  const [wsConnected, setWsConnected] = useState(false);

  useEffect(() => {
    if (!connected || !sessionId) {
      setWsConnected(false);
      return;
    }

    let websocket: WebSocket | null = null;

    try {
      const wsUrl = `${WS_BASE}/metrics/${sessionId}`;
      websocket = new WebSocket(wsUrl);

      websocket.onopen = () => {
        setWsConnected(true);
      };

      websocket.onmessage = (event) => {
        try {
          const message = JSON.parse(event.data);
          if (message.type === "metrics" && message.data) {
            const m = message.data;
            setMetrics({
              framesReceived: m.frames_received || 0,
              packetsReceived: m.packets_received || 0,
              bytesReceived: m.bytes_received || 0,
              currentBitrateMbps: m.bitrate_mbps || 0,
              jitterMs: m.jitter_ms || 0,
              lossPercent: m.loss_percent || 0,
              rttMs: m.rtt_ms || 0,
              streamActive: Boolean(m.stream_active),
            });
          }
        } catch (err) {
          console.error("Failed to parse metrics:", err);
        }
      };

      websocket.onerror = () => {
        setWsConnected(false);
      };

      websocket.onclose = () => {
        setWsConnected(false);
      };

      return () => {
        if (websocket) {
          websocket.close();
        }
      };
    } catch (err) {
      console.error("WebSocket connection error:", err);
      setWsConnected(false);
    }
  }, [connected, sessionId]);

  const reset = useCallback(() => {
    setMetrics(DEFAULT_METRICS);
  }, []);

  return { metrics, wsConnected, reset };
}
