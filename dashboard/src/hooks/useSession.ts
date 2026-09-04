import { useState, useCallback } from "react";
import type { SessionState } from "../types";

const API_BASE = import.meta.env.VITE_API_BASE || "http://localhost:8000/api";

const DEFAULT_SESSION: SessionState = {
  server: "127.0.0.1",
  port: 5000,
  connected: false,
};

export function useSession() {
  const [session, setSession] = useState<SessionState>(DEFAULT_SESSION);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string>("");

  const connect = useCallback(async (server: string, port: number) => {
    setLoading(true);
    setError("");
    try {
      const response = await fetch(`${API_BASE}/sessions`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ server, port }),
      });

      if (!response.ok) {
        throw new Error("Failed to create session");
      }

      const data = await response.json();
      setSession({
        server,
        port,
        connected: true,
        sessionId: data.session_id,
      });
    } catch (err) {
      const msg = err instanceof Error ? err.message : "Connection failed";
      setError(msg);
      console.error("Session connect error:", msg);
    } finally {
      setLoading(false);
    }
  }, []);

  const disconnect = useCallback(async () => {
    if (!session.sessionId) return;
    try {
      await fetch(`${API_BASE}/sessions/${session.sessionId}`, {
        method: "DELETE",
      });
    } catch (err) {
      console.error("Disconnect error:", err);
    } finally {
      setSession((prev) => ({ ...prev, connected: false, sessionId: undefined }));
    }
  }, [session.sessionId]);

  return {
    session,
    loading,
    error,
    connect,
    disconnect,
  };
}
