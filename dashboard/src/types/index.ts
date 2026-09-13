export interface SessionState {
  server: string;
  port: number;
  connected: boolean;
  sessionId?: string;
}

export interface StreamMetrics {
  framesReceived: number;
  packetsReceived: number;
  bytesReceived: number;
  currentBitrateMbps: number;
  jitterMs: number;
  lossPercent: number;
  rttMs: number;
  streamActive: boolean;
}

export interface StreamFrame {
  timestamp: number;
  data: Uint8Array;
}

export interface ApiResponse<T> {
  success: boolean;
  data?: T;
  error?: string;
}
