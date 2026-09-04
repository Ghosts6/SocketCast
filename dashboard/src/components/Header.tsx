import { useState, useRef } from "react";
import { useTheme } from "../contexts/ThemeContext";

interface HeaderProps {
  connected: boolean;
  className?: string;
}

export function Header({ connected, className }: HeaderProps) {
  const { theme, toggleTheme } = useTheme();
  const [showTooltip, setShowTooltip] = useState(false);
  const tooltipTimeoutRef = useRef<ReturnType<typeof setTimeout>>();

  const handleMouseEnter = () => {
    tooltipTimeoutRef.current = setTimeout(() => {
      setShowTooltip(true);
    }, 300);
  };

  const handleMouseLeave = () => {
    if (tooltipTimeoutRef.current) {
      clearTimeout(tooltipTimeoutRef.current);
    }
    setShowTooltip(false);
  };

  return (
    <header className={`sticky top-0 z-20 bg-white/95 dark:bg-neutral-900/95 backdrop-blur-md border-b border-neutral-200 dark:border-neutral-800 py-4 px-4 md:px-6 transition-colors duration-200 ${className || ""}`}>
      <div className="max-w-7xl mx-auto flex items-center justify-between">
        <div className="min-w-0 flex items-center gap-3">
          <img
            src="/img/logo.png"
            alt="SocketCast Logo"
            className="h-8 w-8 md:h-10 md:w-10 object-contain"
          />
          <div>
            <h1 className="text-xl md:text-3xl font-bold text-neutral-900 dark:text-white tracking-tight">SocketCast</h1>
            <p className="text-xs md:text-sm text-neutral-600 dark:text-neutral-400 mt-0.5">UDP Streaming Dashboard</p>
          </div>
        </div>

        <div className="flex items-center gap-3 ml-4 shrink-0">
          {/* Theme Toggle with Tooltip */}
          <div className="relative">
            <button
              onClick={toggleTheme}
              onMouseEnter={handleMouseEnter}
              onMouseLeave={handleMouseLeave}
              className="p-2 text-xl md:text-2xl rounded-lg hover:bg-neutral-100 dark:hover:bg-neutral-800 transition-colors duration-200 focus:outline-none focus:ring-2 focus:ring-blue-500/50"
              aria-label={`Switch to ${theme === "dark" ? "light" : "dark"} mode`}
            >
              {theme === "dark" ? "☀️" : "🌙"}
            </button>

            {/* Custom Tooltip */}
            {showTooltip && (
              <div className="absolute top-full left-1/2 -translate-x-1/2 mt-2 px-3 py-1.5 bg-neutral-100 dark:bg-neutral-900 text-neutral-900 dark:text-white text-xs font-medium rounded whitespace-nowrap pointer-events-none animate-in fade-in-0 zoom-in-95 duration-200 border border-neutral-300 dark:border-neutral-700">
                {theme === "dark" ? "Light mode" : "Dark mode"}
                <div className="absolute bottom-full left-1/2 -translate-x-1/2 border-4 border-transparent border-b-neutral-400 dark:border-b-neutral-600" />
              </div>
            )}
          </div>

          {/* Connection Status */}
          <div className="text-right">
            <div className={`text-xs md:text-sm font-medium flex items-center gap-2 ${connected ? "text-green-600 dark:text-green-400" : "text-neutral-500 dark:text-neutral-500"}`}>
              <span className={`w-2 h-2 rounded-full ${connected ? "bg-green-400 animate-pulse" : "bg-neutral-600"}`} />
              <span className="hidden sm:inline">{connected ? "Connected" : "Offline"}</span>
              <span className="sm:hidden">{connected ? "ON" : "OFF"}</span>
            </div>
          </div>
        </div>
      </div>
    </header>
  );
}
