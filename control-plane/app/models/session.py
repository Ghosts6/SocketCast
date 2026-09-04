"""Pydantic models for session state and metrics."""
from datetime import datetime
from pydantic import BaseModel, Field, ConfigDict


class Session(BaseModel):
    model_config = ConfigDict()

    session_id: str
    stream_id: int
    server: str
    port: int
    state: str = "handshaking"
    created_at: str = Field(default_factory=lambda: datetime.utcnow().isoformat())
    connected_at: str = None


class StreamMetrics(BaseModel):
    model_config = ConfigDict()

    session_id: str
    bitrate_mbps: float = 0.0
    rtt_ms: float = 0.0
    jitter_ms: float = 0.0
    loss_percent: float = 0.0
    frames_received: int = 0
    packets_received: int = 0
    bytes_received: int = 0
    timestamp: str = Field(default_factory=lambda: datetime.utcnow().isoformat())
