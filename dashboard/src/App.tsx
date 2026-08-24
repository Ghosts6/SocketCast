// Control plane: talks to FastAPI over REST/WS for session mgmt + metrics.
// Video plane: renders the WS-bridged stream to a <canvas>.
// See Doc/dev/04-architecture-and-tech-decisions.md, "Frontend architecture".
//
// TODO(Phase 6): split into components/ (SessionControls, MetricsPanel,
// StreamCanvas) and hooks/ (useSession, useMetricsSocket, useStreamSocket).

function App() {
  return (
    <div className="min-h-screen bg-neutral-950 text-neutral-100 flex items-center justify-center">
      <p className="text-lg">SocketCast dashboard — placeholder</p>
    </div>
  );
}

export default App;
