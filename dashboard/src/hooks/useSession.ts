import { useState, useCallback } from "react";
import type { SessionState } from "../types";

const DEFAULT_SESSION: SessionState = {
  server: "127.0.0.1",
  port: 5000,
  connected: false,
};

export function useSession() {
  const [session, setSession] = useState<SessionState>(DEFAULT_SESSION);
  const [loading, setLoading] = useState(false);

  const connect = useCallback(async (server: string, port: number) => {
    setLoading(true);
    try {
      setSession({
        server,
        port,
        connected: true,
        sessionId: `session-${Date.now()}`,
      });
    } catch (error) {
      console.error("Connection failed:", error);
    } finally {
      setLoading(false);
    }
  }, []);

  const disconnect = useCallback(async () => {
    try {
      setSession((prev) => ({ ...prev, connected: false, sessionId: undefined }));
    } catch (error) {
      console.error("Disconnect failed:", error);
    }
  }, []);

  return {
    session,
    loading,
    connect,
    disconnect,
  };
}
