import { useRef, useEffect, useState } from "react";
import { useStream } from "../hooks/useStream";

interface StreamCanvasProps {
  connected: boolean;
  sessionId?: string;
}

function MaximizeIcon() {
  return (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" className="w-4 h-4">
      <path d="M8 3H5a2 2 0 0 0-2 2v3" />
      <path d="M21 8V5a2 2 0 0 0-2-2h-3" />
      <path d="M3 16v3a2 2 0 0 0 2 2h3" />
      <path d="M16 21h3a2 2 0 0 0 2-2v-3" />
    </svg>
  );
}

function MinimizeIcon() {
  return (
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" className="w-4 h-4">
      <path d="M8 3v3a2 2 0 0 1-2 2H3" />
      <path d="M21 8h-3a2 2 0 0 1-2-2V3" />
      <path d="M3 16h3a2 2 0 0 1 2 2v3" />
      <path d="M16 21v-3a2 2 0 0 1 2-2h3" />
    </svg>
  );
}

export function StreamCanvas({ connected, sessionId }: StreamCanvasProps) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);
  const [fps, setFps] = useState(0);
  const lastCountRef = useRef(0);
  const [isFullscreen, setIsFullscreen] = useState(false);

  const { frameCount } = useStream(canvasRef, connected, sessionId);

  useEffect(() => {
    const handleFullscreenChange = () => {
      setIsFullscreen(document.fullscreenElement === containerRef.current);
    };
    document.addEventListener("fullscreenchange", handleFullscreenChange);
    return () => document.removeEventListener("fullscreenchange", handleFullscreenChange);
  }, []);

  const toggleFullscreen = () => {
    if (!containerRef.current) return;
    if (document.fullscreenElement) {
      document.exitFullscreen().catch(() => {});
    } else {
      containerRef.current.requestFullscreen().catch(() => {});
    }
  };

  // FPS counter
  useEffect(() => {
    if (!connected) return;

    const interval = setInterval(() => {
      const current = frameCount.current;
      setFps(current - lastCountRef.current);
      lastCountRef.current = current;
    }, 1000);

    return () => clearInterval(interval);
  }, [connected, frameCount]);

  useEffect(() => {
    if (canvasRef.current && !connected) {
      const ctx = canvasRef.current.getContext("2d");
      if (ctx) {
        ctx.fillStyle = "#1a1a1a";
        ctx.fillRect(0, 0, canvasRef.current.width, canvasRef.current.height);
        ctx.fillStyle = "#666";
        ctx.font = "18px sans-serif";
        ctx.textAlign = "center";
        ctx.textBaseline = "middle";
        ctx.fillText("⏸ Waiting for stream...", canvasRef.current.width / 2, canvasRef.current.height / 2);
      }
    }
  }, [connected]);

  return (
    <div className="bg-white dark:bg-neutral-900 rounded-lg overflow-hidden shadow-lg border border-neutral-200 dark:border-neutral-800 hover:border-neutral-300 dark:hover:border-neutral-700 transition-colors">
      {/* Canvas */}
      <div
        ref={containerRef}
        className={`relative bg-black flex items-center justify-center overflow-hidden group ${
          isFullscreen ? "w-screen h-screen" : "aspect-video"
        }`}
      >
        <canvas
          ref={canvasRef}
          width={1280}
          height={720}
          className="w-full h-full object-contain bg-black"
          role="img"
          aria-label="Video stream canvas"
        />

        {/* Stream indicator overlay */}
        <div className="absolute top-3 left-3 flex gap-2 items-center opacity-0 group-hover:opacity-100 transition-opacity">
          {connected && (
            <>
              <div className="w-2 h-2 bg-red-500 rounded-full animate-pulse" />
              <span className="text-xs font-semibold text-red-400 bg-black/50 px-2 py-1 rounded">LIVE</span>
            </>
          )}
        </div>

        {/* Fullscreen controls */}
        <div className="absolute top-3 right-3 flex gap-2 opacity-0 group-hover:opacity-100 transition-opacity">
          <button
            type="button"
            onClick={toggleFullscreen}
            className="p-1.5 rounded bg-black/50 text-white hover:bg-black/70 transition-colors"
            aria-label={isFullscreen ? "Exit fullscreen" : "Enter fullscreen"}
            title={isFullscreen ? "Exit fullscreen" : "Enter fullscreen"}
          >
            {isFullscreen ? <MinimizeIcon /> : <MaximizeIcon />}
          </button>
        </div>
      </div>

      {/* Footer */}
      <div className="p-3 md:p-4 bg-neutral-100 dark:bg-neutral-800 border-t border-neutral-200 dark:border-neutral-700 flex flex-col sm:flex-row justify-between gap-2 text-xs md:text-sm text-neutral-700 dark:text-neutral-300">
        <div className="flex items-center gap-2">
          <div className={`w-2 h-2 rounded-full ${connected ? "bg-green-400 animate-pulse" : "bg-neutral-600"}`} />
          <span className={connected ? "text-green-400 font-medium" : "text-neutral-400"}>
            {connected ? "Streaming" : "Idle"}
          </span>
        </div>
        <div className="text-neutral-400 space-x-4">
          <span className="hidden sm:inline">1280×720 H.264</span>
          {connected && <span className="text-yellow-400">{fps} FPS</span>}
        </div>
      </div>
    </div>
  );
}
