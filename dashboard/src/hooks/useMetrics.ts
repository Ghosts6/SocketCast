import { useState, useEffect, useCallback } from "react";
import type { StreamMetrics } from "../types";

const DEFAULT_METRICS: StreamMetrics = {
  framesReceived: 0,
  packetsReceived: 0,
  bytesReceived: 0,
  currentBitrateMbps: 0,
  jitterMs: 0,
  lossPercent: 0,
  rttMs: 0,
};

export function useMetrics(connected: boolean) {
  const [metrics, setMetrics] = useState<StreamMetrics>(DEFAULT_METRICS);
  const [wsConnected, setWsConnected] = useState(false);

  useEffect(() => {
    if (!connected) return;

    // Phase 5: Replace with real WebSocket to FastAPI metrics endpoint
    // For now, simulate with incremental updates
    const interval = setInterval(() => {
      setMetrics((prev) => ({
        framesReceived: prev.framesReceived + Math.random() > 0.8 ? 1 : 0,
        packetsReceived: prev.packetsReceived + (Math.floor(Math.random() * 10) + 5),
        bytesReceived: prev.bytesReceived + Math.floor(Math.random() * 50000),
        currentBitrateMbps: 1.0 + (Math.random() - 0.5) * 0.2,
        jitterMs: 45 + (Math.random() - 0.5) * 20,
        lossPercent: Math.max(0, (Math.random() - 0.95) * 2),
        rttMs: 50 + (Math.random() - 0.5) * 30,
      }));
    }, 1000);

    setWsConnected(true);
    return () => {
      clearInterval(interval);
      setWsConnected(false);
    };
  }, [connected]);

  const reset = useCallback(() => {
    setMetrics(DEFAULT_METRICS);
  }, []);

  return { metrics, wsConnected, reset };
}
