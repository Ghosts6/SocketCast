# SocketCast Observability

SocketCast ships with Prometheus metrics and a pre-provisioned Grafana dashboard so you can monitor the control plane, sessions, streams, and WebSocket traffic in real time.

## Quick start

Start the full stack:

```bash
docker compose up -d --build
```

| Service | URL | Notes |
| --- | --- | --- |
| Operator dashboard | http://localhost:5173 | Live stream UI |
| Control-plane API | http://localhost:8000 | REST + WebSockets |
| Prometheus metrics (raw) | http://localhost:8000/metrics | Scrape target for Prometheus |
| Prometheus UI | http://localhost:9090 | Query and explore time series |
| Grafana | http://localhost:3000 | Dashboards (default login below) |

**Grafana login (development defaults):** username `admin`, password `admin`. Change these before any shared or production deployment.

---

## Architecture

```
┌─────────────────┐     scrape /metrics      ┌────────────┐
│  Control plane  │ ◄─────────────────────── │ Prometheus │
│  :8000          │                          │  :9090     │
└────────┬────────┘                          └─────┬──────┘
         │ instrumented HTTP / WS / Redis / engine  │
         ▼                                          ▼
┌─────────────────┐                          ┌────────────┐
│  Engine / Redis │                          │  Grafana   │
└─────────────────┘                          │  :3000     │
                                             └────────────┘
```

- The **control plane** exposes Prometheus-format metrics at `GET /metrics`.
- **Prometheus** scrapes that endpoint every 15 seconds (see `prometheus.yml`).
- **Grafana** is provisioned with a Prometheus data source and the **SocketCast Overview** dashboard.

---

## Metrics reference

All metric names use the `socketcast_` prefix. Types follow the [Prometheus metric types](https://prometheus.io/docs/concepts/metric_types/).

### HTTP

| Metric | Type | Labels | Description |
| --- | --- | --- | --- |
| `socketcast_requests_total` | Counter | `method`, `endpoint`, `status` | Total HTTP requests |
| `socketcast_request_duration_seconds` | Histogram | `method`, `endpoint` | Request latency |

### Sessions

| Metric | Type | Labels | Description |
| --- | --- | --- | --- |
| `socketcast_active_sessions` | Gauge | - | Sessions currently registered |
| `socketcast_sessions_created_total` | Counter | - | Lifetime session creations |

### Client metrics API

| Metric | Type | Labels | Description |
| --- | --- | --- | --- |
| `socketcast_metrics_received_total` | Counter | - | Metrics payloads accepted via `POST /api/metrics/{session_id}` |

### Streams

| Metric | Type | Labels | Description |
| --- | --- | --- | --- |
| `socketcast_active_streams` | Gauge | - | Streams currently active via admin API |
| `socketcast_streams_created_total` | Counter | - | Lifetime stream starts |
| `socketcast_frames_received_total` | Counter | - | Media frames forwarded to the dashboard |

### WebSockets

| Metric | Type | Labels | Description |
| --- | --- | --- | --- |
| `socketcast_websocket_connections` | Gauge | `type` (`metrics` or `stream`) | Open WebSocket connections |

### Errors and connectivity

| Metric | Type | Labels | Description |
| --- | --- | --- | --- |
| `socketcast_errors_total` | Counter | `type` | Application errors by category |
| `socketcast_redis_errors_total` | Counter | - | Redis connectivity failures |
| `socketcast_engine_errors_total` | Counter | - | Engine admin / frame-fetch failures |

Example PromQL:

```promql
# Request rate (per second, 5-minute window)
rate(socketcast_requests_total[5m])

# Active sessions
socketcast_active_sessions

# Error rate
rate(socketcast_errors_total[5m])
```

---

## Grafana

### Open the dashboard

1. Go to http://localhost:3000 and sign in.
2. Open **Dashboards** → **SocketCast Overview**.

The default dashboard includes:

- Active sessions over time  
- HTTP request rate (rolling average)  
- Active streams  
- Total errors  

### Customize

Dashboard JSON lives at:

```text
grafana/provisioning/dashboards/socketcast.json
```

After editing, reload Grafana:

```bash
docker compose restart grafana
```

The Prometheus data source is provisioned from `grafana/provisioning/datasources/prometheus.yml` and points at `http://prometheus:9090` inside the Compose network.

---

## Prometheus configuration

Scrape jobs are defined in `prometheus.yml` at the repository root:

| Job | Target | Path | Interval |
| --- | --- | --- | --- |
| `control-plane` | `control-plane:8000` | `/metrics` | 15s |
| `engine` | `engine:5001` | `/admin/metrics` | 15s |

The control-plane job is the primary source today. The engine job is reserved for a native engine metrics endpoint; until that endpoint is available, Prometheus may report that target as down. That does not affect control-plane metrics or Grafana panels that use them.

To verify scrapes:

1. Open http://localhost:9090/targets  
2. Confirm `control-plane` is **UP**

---

## Where metrics are recorded

| Area | Location | What is counted |
| --- | --- | --- |
| HTTP middleware | `control-plane/app/core/middleware.py` | Request count, duration, request errors |
| Sessions | `control-plane/app/api/sessions.py` | Active sessions, creations, failures |
| Metrics API | `control-plane/app/api/metrics.py` | Posted client metrics, update failures |
| Admin / streams | `control-plane/app/api/admin.py` | Stream create/stop, engine connection errors |
| WebSocket bridge | `control-plane/app/bridge/websocket_bridge.py` | Connection gauges, frames forwarded, bridge/engine errors |

Metric definitions live in `control-plane/app/core/metrics.py`.

---

## Production checklist

- [ ] Change Grafana `admin` credentials (`GF_SECURITY_ADMIN_USER` / `GF_SECURITY_ADMIN_PASSWORD` in Compose or your orchestrator).
- [ ] Do not expose Grafana, Prometheus, or `/metrics` on the public internet without authentication or network controls.
- [ ] Persist Prometheus and Grafana volumes (Compose already defines `prometheus-data` and `grafana-data`).
- [ ] Confirm `control-plane` appears **UP** under Prometheus → Status → Targets.
- [ ] Optionally disable or remove the `engine` scrape job until the engine exposes `/admin/metrics`.
- [ ] Add alert rules for sustained error rate, Redis failures, and engine connectivity as your SLOs require.

---

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| Grafana shows “No data” | Prometheus target UP? Data source URL `http://prometheus:9090`? Time range set to “Last 15 minutes”? |
| `control-plane` target DOWN | Is the control-plane container healthy? Can Prometheus resolve `control-plane:8000` on the Compose network? |
| `engine` target DOWN | Expected until the engine serves `/admin/metrics`. Safe to ignore or remove that job. |
| Metrics missing after deploy | Hit http://localhost:8000/metrics directly; restart Prometheus if config changed: `docker compose restart prometheus` |
