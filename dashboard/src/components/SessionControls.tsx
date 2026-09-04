import { useState } from "react";
import type { SessionState } from "../types";

interface SessionControlsProps {
  session: SessionState;
  loading: boolean;
  onConnect: (server: string, port: number) => void;
  onDisconnect: () => void;
}

function InputField({
  id,
  label,
  icon,
  value,
  onChange,
  disabled,
  type = "text",
  placeholder,
  min,
  max,
}: {
  id: string;
  label: string;
  icon: string;
  value: string;
  onChange: (e: React.ChangeEvent<HTMLInputElement>) => void;
  disabled: boolean;
  type?: string;
  placeholder?: string;
  min?: number;
  max?: number;
}) {
  return (
    <div>
      <label htmlFor={id} className="block text-xs text-neutral-600 dark:text-neutral-400 mb-2 font-medium">
        {label}
      </label>
      <div className="relative">
        <span className="absolute left-3 top-1/2 -translate-y-1/2 text-neutral-400 dark:text-neutral-500 text-lg">{icon}</span>
        <input
          id={id}
          type={type}
          value={value}
          onChange={onChange}
          disabled={disabled}
          className="w-full bg-neutral-50 dark:bg-neutral-800/50 border border-neutral-300 dark:border-neutral-700 hover:border-neutral-400 dark:hover:border-neutral-600 focus:border-blue-500 rounded-lg pl-11 pr-4 py-3 text-sm text-neutral-900 dark:text-neutral-100 placeholder-neutral-500 dark:placeholder-neutral-600 disabled:opacity-50 disabled:cursor-not-allowed focus:outline-none focus:ring-2 focus:ring-blue-500/20 transition-all duration-200"
          placeholder={placeholder}
          min={min}
          max={max}
          autoComplete="off"
        />
      </div>
    </div>
  );
}

export function SessionControls({ session, loading, onConnect, onDisconnect }: SessionControlsProps) {
  const [server, setServer] = useState(session.server);
  const [port, setPort] = useState(session.port.toString());
  const [error, setError] = useState<string>("");
  const [touched, setTouched] = useState({ server: false, port: false });

  const isValidPort = (p: string) => {
    const portNum = parseInt(p, 10);
    return !isNaN(portNum) && portNum > 0 && portNum < 65536;
  };

  const isValidServer = (s: string) => s.trim().length > 0;

  const handleConnect = () => {
    setError("");
    if (!isValidServer(server)) {
      setError("Server address is required");
      setTouched({ server: true, port: touched.port });
      return;
    }

    if (!isValidPort(port)) {
      setError("Port must be between 1 and 65535");
      setTouched({ server: touched.server, port: true });
      return;
    }

    onConnect(server, parseInt(port, 10));
  };

  const handleKeyPress = (e: React.KeyboardEvent) => {
    if (e.key === "Enter" && !session.connected && !loading) {
      handleConnect();
    }
  };

  return (
    <div className="bg-white dark:bg-neutral-900/50 backdrop-blur-sm rounded-lg p-4 md:p-6 border border-neutral-200 dark:border-neutral-800 hover:border-neutral-300 dark:hover:border-neutral-700/50 transition-all duration-200 shadow-lg">
      <div className="flex items-center gap-2 mb-4 md:mb-6">
        <div className="text-lg">🔌</div>
        <h3 className="text-sm font-semibold text-neutral-700 dark:text-neutral-300 uppercase tracking-wider">Session Control</h3>
      </div>

      <div className="space-y-4">
        {/* Server Input */}
        <InputField
          id="server"
          label="Server Address"
          icon="🌐"
          value={server}
          onChange={(e) => {
            setServer(e.target.value);
            setError("");
            setTouched({ ...touched, server: true });
          }}
          disabled={session.connected}
          placeholder="127.0.0.1 or hostname"
        />
        {touched.server && !isValidServer(server) && (
          <p className="text-xs text-red-400">Server address cannot be empty</p>
        )}

        {/* Port Input */}
        <InputField
          id="port"
          label="Port Number"
          icon="📡"
          value={port}
          onChange={(e) => {
            setPort(e.target.value);
            setError("");
            setTouched({ ...touched, port: true });
          }}
          disabled={session.connected}
          type="number"
          placeholder="5000"
          min={1}
          max={65535}
        />
        {touched.port && !isValidPort(port) && (
          <p className="text-xs text-red-400">Port must be between 1 and 65535</p>
        )}

        {/* Error Message */}
        {error && (
          <div className="p-3 bg-red-50 dark:bg-red-500/10 border border-red-200 dark:border-red-500/30 rounded-lg text-xs text-red-600 dark:text-red-400 animate-in fade-in flex items-center gap-2">
            <span>⚠️</span>
            <span>{error}</span>
          </div>
        )}

        {/* Action Buttons */}
        <div className="flex gap-2 pt-2">
          {!session.connected ? (
            <button
              onClick={handleConnect}
              onKeyPress={handleKeyPress}
              disabled={loading}
              className="flex-1 bg-gradient-to-r from-blue-600 to-blue-700 hover:from-blue-700 hover:to-blue-800 disabled:opacity-50 disabled:cursor-not-allowed text-white font-medium py-3 px-4 rounded-lg text-sm transition-all duration-200 focus:outline-none focus:ring-2 focus:ring-blue-500 focus:ring-offset-2 focus:ring-offset-neutral-900 shadow-lg hover:shadow-blue-500/20 active:scale-95"
              aria-busy={loading}
            >
              {loading ? (
                <span className="flex items-center justify-center gap-2">
                  <span className="w-3 h-3 border-2 border-white/30 border-t-white rounded-full animate-spin" />
                  Connecting...
                </span>
              ) : (
                "▶ Connect"
              )}
            </button>
          ) : (
            <>
              <button
                onClick={onDisconnect}
                disabled={loading}
                className="flex-1 bg-gradient-to-r from-red-600 to-red-700 hover:from-red-700 hover:to-red-800 disabled:opacity-50 disabled:cursor-not-allowed text-white font-medium py-3 px-4 rounded-lg text-sm transition-all duration-200 focus:outline-none focus:ring-2 focus:ring-red-500 focus:ring-offset-2 focus:ring-offset-neutral-900 shadow-lg hover:shadow-red-500/20 active:scale-95"
              >
                ⏹ Disconnect
              </button>
            </>
          )}
        </div>

        {/* Session Info */}
        {session.connected && session.sessionId && (
          <div className="bg-gradient-to-r from-green-50 dark:from-green-500/10 to-emerald-50 dark:to-emerald-500/10 rounded-lg p-4 border border-green-200 dark:border-green-500/30 animate-in fade-in">
            <div className="text-xs text-neutral-600 dark:text-neutral-400 mb-2 font-semibold uppercase tracking-wider">Session ID</div>
            <code className="text-xs text-green-600 dark:text-green-400 break-all font-mono bg-neutral-100 dark:bg-neutral-800/50 p-2 rounded block">
              {session.sessionId}
            </code>
          </div>
        )}
      </div>
    </div>
  );
}
