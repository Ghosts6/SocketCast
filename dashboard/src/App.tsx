import { lazy, Suspense } from "react";
import { Header } from "./components/Header";
import { StreamCanvas } from "./components/StreamCanvas";
import { SessionControls } from "./components/SessionControls";
import { ParticleBackground } from "./components/ParticleBackground";
import { useSession } from "./hooks/useSession";
import { useMetrics } from "./hooks/useMetrics";

const MetricsPanel = lazy(() => import("./components/MetricsPanel").then(m => ({ default: m.MetricsPanel })));

function LoadingSpinner() {
  return (
    <div className="bg-neutral-900/50 dark:bg-neutral-900/50 backdrop-blur-sm rounded-lg p-8 flex items-center justify-center">
      <div className="text-center">
        <div className="inline-block w-8 h-8 border-4 border-neutral-700 border-t-blue-500 rounded-full animate-spin mb-3" />
        <p className="text-sm text-neutral-400">Loading metrics...</p>
      </div>
    </div>
  );
}

function App() {
  const { session, loading, connect, disconnect } = useSession();
  const { metrics, wsConnected } = useMetrics(session.connected);

  return (
    <div className="min-h-screen text-neutral-100 dark:text-neutral-100 flex flex-col overflow-x-hidden relative">
      <ParticleBackground />

      <Header connected={session.connected} className="relative z-20" />

      <main className="flex-1 p-4 md:p-6 overflow-y-auto relative z-10">
        <div className="max-w-7xl mx-auto grid grid-cols-1 lg:grid-cols-3 gap-4 md:gap-6">
          {/* Left: Stream + Session Control */}
          <div className="lg:col-span-2 space-y-4 md:space-y-6">
            <div className="transition-all duration-300">
              <StreamCanvas connected={session.connected} />
            </div>

            <div className="transition-all duration-300">
              <SessionControls
                session={session}
                loading={loading}
                onConnect={connect}
                onDisconnect={disconnect}
              />
            </div>
          </div>

          {/* Right: Metrics Panel (Lazy loaded) */}
          <div className="lg:col-span-1">
            <Suspense fallback={<LoadingSpinner />}>
              <div className="sticky top-20">
                <MetricsPanel metrics={metrics} connected={session.connected && wsConnected} />
              </div>
            </Suspense>
          </div>
        </div>
      </main>

      {/* Footer */}
      <footer className="border-t border-neutral-200 dark:border-neutral-800 py-4 px-4 md:px-6 shrink-0 relative z-20 bg-white/95 dark:bg-neutral-900/95 backdrop-blur-md">
        <div className="max-w-7xl mx-auto">
          <div className="flex flex-col md:flex-row justify-between items-center gap-3 mb-3">
            <div className="text-sm font-medium text-neutral-700 dark:text-neutral-300">
              {session.connected ? (
                <span className="text-green-600 dark:text-green-400 flex items-center gap-2">
                  <span>🔗</span>
                  <span>{session.server}:{session.port}</span>
                </span>
              ) : (
                <span className="text-neutral-600 dark:text-neutral-500">Awaiting connection</span>
              )}
            </div>
          </div>
          <div className="text-xs text-neutral-600 dark:text-neutral-400 text-center border-t border-neutral-200 dark:border-neutral-800 pt-3">
            <div className="mb-1 font-medium text-neutral-700 dark:text-neutral-300">SocketCast v1.0 - Reliable UDP Streaming Dashboard</div>
            <div className="opacity-70">© {new Date().getFullYear()} by ghosts6</div>
          </div>
        </div>
      </footer>
    </div>
  );
}

export default App;
