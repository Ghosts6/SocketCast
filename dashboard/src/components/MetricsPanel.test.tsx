import { describe, it, expect } from "vitest";
import { render, screen } from "@testing-library/react";
import { MetricsPanel } from "./MetricsPanel";
import type { StreamMetrics } from "../types";

const baseMetrics: StreamMetrics = {
  framesReceived: 100,
  packetsReceived: 200,
  bytesReceived: 1024 * 1024,
  currentBitrateMbps: 2.5,
  jitterMs: 3,
  lossPercent: 0,
  rttMs: 20,
  streamActive: true,
};

describe("MetricsPanel", () => {
  it("shows Connected when connected, Disconnected otherwise", () => {
    const { rerender } = render(<MetricsPanel metrics={baseMetrics} connected={true} />);
    expect(screen.getByText("Connected")).toBeInTheDocument();

    rerender(<MetricsPanel metrics={baseMetrics} connected={false} />);
    expect(screen.getByText("Disconnected")).toBeInTheDocument();
  });

  it("shows the no-stream banner only when connected but no stream is active", () => {
    render(<MetricsPanel metrics={{ ...baseMetrics, streamActive: false }} connected={true} />);
    expect(screen.getByText(/No stream running/)).toBeInTheDocument();
  });

  it("hides the no-stream banner when a stream is active", () => {
    render(<MetricsPanel metrics={{ ...baseMetrics, streamActive: true }} connected={true} />);
    expect(screen.queryByText(/No stream running/)).not.toBeInTheDocument();
  });

  it("hides the no-stream banner when not connected at all", () => {
    // connected=false means "idle", not "stream should be running but isn't"
    render(<MetricsPanel metrics={{ ...baseMetrics, streamActive: false }} connected={false} />);
    expect(screen.queryByText(/No stream running/)).not.toBeInTheDocument();
  });

  it("shows a network-degradation warning when loss or RTT is high", () => {
    render(
      <MetricsPanel metrics={{ ...baseMetrics, lossPercent: 10 }} connected={true} />,
    );
    expect(screen.getByText(/Network degradation detected/)).toBeInTheDocument();
  });

  it("suppresses the degradation warning while no stream is active", () => {
    // Avoids flagging "degraded network" for numbers that are just placeholder zeros/stale.
    render(
      <MetricsPanel
        metrics={{ ...baseMetrics, lossPercent: 10, streamActive: false }}
        connected={true}
      />,
    );
    expect(screen.queryByText(/Network degradation detected/)).not.toBeInTheDocument();
  });

  it("renders counters formatted with locale separators and MB conversion", () => {
    render(
      <MetricsPanel
        metrics={{ ...baseMetrics, framesReceived: 12345, bytesReceived: 2 * 1024 * 1024 }}
        connected={true}
      />,
    );
    expect(screen.getByText("12,345")).toBeInTheDocument();
    expect(screen.getByText("2.00 MB")).toBeInTheDocument();
  });
});
