"""Pydantic models for session state."""
from pydantic import BaseModel


class Session(BaseModel):
    session_id: str
    stream_id: int
    state: str = "handshaking"  # mirrors socketcast::SessionState in the engine
