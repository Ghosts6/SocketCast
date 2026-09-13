"""Prometheus metrics for SocketCast control plane."""
from prometheus_client import Counter, Gauge, Histogram

# Request metrics
request_count = Counter(
    "socketcast_requests_total",
    "Total HTTP requests",
    ["method", "endpoint", "status"],
)

request_duration = Histogram(
    "socketcast_request_duration_seconds",
    "HTTP request duration",
    ["method", "endpoint"],
)

# Session metrics
active_sessions = Gauge(
    "socketcast_active_sessions",
    "Number of active sessions",
)

session_created = Counter(
    "socketcast_sessions_created_total",
    "Total sessions created",
)

# Metrics data metrics
metrics_received = Counter(
    "socketcast_metrics_received_total",
    "Total metrics received from clients",
)

# Stream metrics
active_streams = Gauge(
    "socketcast_active_streams",
    "Number of active streams",
)

stream_created = Counter(
    "socketcast_streams_created_total",
    "Total streams created",
)

frames_received = Counter(
    "socketcast_frames_received_total",
    "Total frames received from engine",
)

audio_frames_received = Counter(
    "socketcast_audio_frames_received_total",
    "Total audio frames received from engine",
)

# WebSocket metrics
websocket_connections = Gauge(
    "socketcast_websocket_connections",
    "Active WebSocket connections",
    ["type"],  # metrics or stream
)

# Error metrics
errors_total = Counter(
    "socketcast_errors_total",
    "Total errors",
    ["type"],
)

# Redis metrics
redis_connection_errors = Counter(
    "socketcast_redis_errors_total",
    "Redis connection errors",
)

# Engine connectivity
engine_connection_errors = Counter(
    "socketcast_engine_errors_total",
    "Engine connectivity errors",
)
