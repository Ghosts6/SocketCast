# SocketCast Observability

## Prometheus Metrics

The control-plane exposes Prometheus metrics at `/metrics` endpoint.

### Metrics Exposed

#### Request Metrics
- `socketcast_requests_total`: Total HTTP requests (labels: method, endpoint, status)
- `socketcast_request_duration_seconds`: HTTP request duration histogram (labels: method, endpoint)

#### Session Metrics
- `socketcast_active_sessions`: Current number of active sessions (gauge)
- `socketcast_sessions_created_total`: Total sessions created (counter)

#### Metrics Data
- `socketcast_metrics_received_total`: Total metrics received from clients (counter)

#### Stream Metrics
- `socketcast_active_streams`: Current number of active streams (gauge)
- `socketcast_streams_created_total`: Total streams created (counter)
- `socketcast_frames_received_total`: Total frames received from engine (counter)

#### WebSocket Metrics
- `socketcast_websocket_connections`: Active WebSocket connections (gauge, labels: type={metrics|stream})

#### Error Metrics
- `socketcast_errors_total`: Total errors (counter, labels: type)

#### Connectivity Metrics
- `socketcast_redis_errors_total`: Redis connection errors (counter)
- `socketcast_engine_errors_total`: Engine connectivity errors (counter)

## Grafana Dashboards

Grafana is automatically provisioned with:
- Prometheus data source pointing to Prometheus container
- Default dashboard showing:
  - Active sessions over time
  - Request rate (5-minute rolling average)
  - Active streams
  - Total errors

### Access Grafana

1. Open `http://localhost:3000`
2. Login with:
   - Username: `admin`
   - Password: `admin`
3. View "SocketCast Overview" dashboard

### Customize Dashboards

Dashboards are defined in `grafana/provisioning/dashboards/socketcast.json`. Edit this file and restart Grafana to apply changes:

```bash
docker-compose restart grafana
```

## Prometheus Configuration

Prometheus scrapes metrics from:
- Control-plane: `http://control-plane:8000/metrics` (15s interval)
- Engine: `http://engine:5001/admin/metrics` (15s interval, Phase 7.2 implementation pending)

Configuration is in `prometheus.yml`.

## Running the Stack

```bash
docker-compose up -d --build
```

Then access:
- Control-plane API: `http://localhost:8000`
- Prometheus: `http://localhost:9090`
- Grafana: `http://localhost:3000`
- Dashboard: `http://localhost:5173`

## Metrics Integration Points

### Control-Plane Instrumentation

1. **MetricsMiddleware** (`app/core/middleware.py`):
   - Tracks all HTTP requests (method, endpoint, status, duration)
   - Tracks request errors

2. **Sessions** (`app/api/sessions.py`):
   - `active_sessions` gauge incremented on create, decremented on delete
   - `session_created` counter incremented on create
   - Error tracking on creation/deletion failures

3. **Metrics** (`app/api/metrics.py`):
   - `metrics_received` counter incremented on POST
   - Error tracking on update failures

4. **Admin** (`app/api/admin.py`):
   - `stream_created` counter incremented on stream start
   - `active_streams` gauge incremented on start, decremented on stop
   - `engine_connection_errors` tracked on engine communication failures
   - Error tracking on stream operations

5. **WebSocket Bridge** (`app/bridge/websocket_bridge.py`):
   - `websocket_connections` gauge tracked per connection type (metrics|stream)
   - `frames_received` counter incremented when frames forwarded to dashboard
   - `engine_connection_errors` tracked on engine fetch failures
   - Error tracking on WebSocket failures

## Future Enhancements

- Phase 7.3: Add request tracing with correlation IDs
- Phase 7.4: Custom performance dashboards
- Phase 8.1: Distributed tracing (OpenTelemetry)
- Phase 8.2: Alert rules (high error rate, slow requests, etc.)
