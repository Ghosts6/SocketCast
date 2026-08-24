"""Session management endpoints (start/stop stream, list active sessions).

TODO(Phase 5): wire up to Redis-backed session state and to the engine's
control channel.
"""
from fastapi import APIRouter

router = APIRouter()


@router.get("/")
async def list_sessions():
    # TODO: read active sessions from Redis.
    return {"sessions": []}


@router.post("/")
async def create_session():
    # TODO: allocate a stream_id, register with the engine, persist to Redis.
    return {"status": "not_implemented"}


@router.delete("/{session_id}")
async def stop_session(session_id: str):
    # TODO: tear down session on the engine, clean up Redis state.
    return {"status": "not_implemented", "session_id": session_id}
