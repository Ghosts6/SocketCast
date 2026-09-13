import type { StreamMetrics } from "../types";

interface MetricsPanelProps {
  metrics: StreamMetrics;
  connected: boolean;
}

function MetricCard({ label, value, unit, trend }: { label: string; value: number | string; unit?: string; trend?: "up" | "down" | "stable" }) {
  const trendColor = {
    up: "text-red-500 dark:text-red-400",
    down: "text-green-500 dark:text-green-400",
    stable: "text-blue-500 dark:text-blue-400",
  }[trend || "stable"];

  return (
    <div className="bg-neutral-50 dark:bg-neutral-800 hover:bg-neutral-100 dark:hover:bg-neutral-750 rounded p-4 transition-colors duration-200 border border-neutral-200 dark:border-neutral-700 hover:border-neutral-300 dark:hover:border-neutral-600">
      <div className="text-xs text-neutral-600 dark:text-neutral-400 uppercase tracking-wider mb-2 font-semibold">{label}</div>
      <div className="flex items-baseline justify-between">
        <div className={`text-3xl font-bold ${trendColor}`}>
          {typeof value === "number" ? value.toFixed(2) : value}
        </div>
        {unit && <span className="text-sm text-neutral-500 dark:text-neutral-500 ml-2">{unit}</span>}
      </div>
    </div>
  );
}

function StatusIndicator({ connected }: { connected: boolean }) {
  return (
    <div className="flex items-center gap-3 px-4 py-3 bg-neutral-100 dark:bg-neutral-800 rounded border border-neutral-200 dark:border-neutral-700">
      <div className={`w-3 h-3 rounded-full ${connected ? "bg-green-500 dark:bg-green-400 animate-pulse" : "bg-neutral-400 dark:bg-neutral-600"}`} />
      <span className={`text-sm font-medium ${connected ? "text-green-600 dark:text-green-400" : "text-neutral-600 dark:text-neutral-400"}`}>
        {connected ? "Connected" : "Disconnected"}
      </span>
    </div>
  );
}

export function MetricsPanel({ metrics, connected }: MetricsPanelProps) {
  const lossWarning = metrics.lossPercent > 5;
  const rttWarning = metrics.rttMs > 100;
  const noStream = connected && !metrics.streamActive;

  return (
    <div className="space-y-4">
      {/* Status */}
      <div className="bg-white dark:bg-neutral-900 rounded-lg p-4 border border-neutral-200 dark:border-neutral-800">
        <h3 className="text-xs font-semibold text-neutral-700 dark:text-neutral-300 uppercase tracking-wider mb-3">Connection Status</h3>
        <StatusIndicator connected={connected} />
      </div>

      {/* Key Metrics */}
      <div className="bg-white dark:bg-neutral-900 rounded-lg p-4 border border-neutral-200 dark:border-neutral-800">
        <h3 className="text-xs font-semibold text-neutral-700 dark:text-neutral-300 uppercase tracking-wider mb-4">Network Metrics</h3>

        {noStream && (
          <div className="mb-3 p-3 bg-blue-50 dark:bg-blue-500/10 border border-blue-200 dark:border-blue-500/30 rounded text-xs text-blue-600 dark:text-blue-400">
            ℹ No stream running, these are placeholder zeros, not live measurements.
            Start one with <code className="font-mono">scripts/start_stream.sh</code>.
          </div>
        )}

        <div className={`grid grid-cols-1 gap-3 ${noStream ? "opacity-50" : ""}`}>
          <MetricCard label="Bitrate" value={metrics.currentBitrateMbps} unit="Mbps" trend="stable" />
          <MetricCard label="RTT" value={metrics.rttMs} unit="ms" trend={rttWarning ? "up" : "stable"} />
          <MetricCard label="Jitter" value={metrics.jitterMs} unit="ms" trend="stable" />
          <MetricCard label="Loss" value={metrics.lossPercent} unit="%" trend={lossWarning ? "up" : "down"} />
        </div>

        {!noStream && (lossWarning || rttWarning) && (
          <div className="mt-3 p-3 bg-orange-50 dark:bg-orange-500/10 border border-orange-200 dark:border-orange-500/30 rounded text-xs text-orange-600 dark:text-orange-400">
            ⚠ Network degradation detected
          </div>
        )}
      </div>

      {/* Counters */}
      <div className="bg-white dark:bg-neutral-900 rounded-lg p-4 border border-neutral-200 dark:border-neutral-800">
        <h3 className="text-xs font-semibold text-neutral-700 dark:text-neutral-300 uppercase tracking-wider mb-4">Counters</h3>
        <div className="space-y-2.5">
          <div className="flex justify-between text-sm">
            <span className="text-neutral-600 dark:text-neutral-400">Frames:</span>
            <span className="text-green-600 dark:text-green-400 font-medium">{metrics.framesReceived.toLocaleString()}</span>
          </div>
          <div className="flex justify-between text-sm">
            <span className="text-neutral-600 dark:text-neutral-400">Packets:</span>
            <span className="text-green-600 dark:text-green-400 font-medium">{metrics.packetsReceived.toLocaleString()}</span>
          </div>
          <div className="flex justify-between text-sm">
            <span className="text-neutral-600 dark:text-neutral-400">Bytes Received:</span>
            <span className="text-green-600 dark:text-green-400 font-medium">{(metrics.bytesReceived / 1024 / 1024).toFixed(2)} MB</span>
          </div>
        </div>
      </div>

      {/* Info Box */}
      <div className="bg-white dark:bg-neutral-900 rounded-lg p-4 border border-neutral-200 dark:border-neutral-800">
        <h3 className="text-xs font-semibold text-neutral-700 dark:text-neutral-300 uppercase tracking-wider mb-2">About</h3>
        <p className="text-xs text-neutral-600 dark:text-neutral-500 leading-relaxed">
          Real-time metrics from the UDP transport layer. All values update every second.
        </p>
      </div>
    </div>
  );
}

export default MetricsPanel;
