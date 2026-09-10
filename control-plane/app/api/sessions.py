"""Session management endpoints (create/list/delete streams)."""
import json
import uuid
from datetime import datetime, UTC
from fastapi import APIRouter, HTTPException
from pydantic import BaseModel

from app.core.redis_client import redis_client
from app.core import metrics
from app.models.session import Session

router = APIRouter()


class SessionCreateRequest(BaseModel):
    server: str
    port: int


@router.get("/")
async def list_sessions():
    """List all active sessions from Redis."""
    try:
        keys = await redis_client.keys("session:*")
        sessions = []
        for key in keys:
            data = await redis_client.get(key)
            if data:
                session = json.loads(data)
                sessions.append(session)
        return {"sessions": sessions, "count": len(sessions)}
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Redis error: {str(e)}")


@router.post("/")
async def create_session(req: SessionCreateRequest):
    """Create and register a new session."""
    try:
        session_id = str(uuid.uuid4())
        stream_id = int(datetime.now(UTC).timestamp() * 1000) & 0xFFFFFF

        session = Session(
            session_id=session_id,
            stream_id=stream_id,
            server=req.server,
            port=req.port,
            state="handshaking",
            created_at=datetime.now(UTC).isoformat(),
        )

        # Persist to Redis
        key = f"session:{session_id}"
        await redis_client.setex(
            key,
            3600,  # 1 hour TTL
            session.model_dump_json()
        )

        metrics.session_created.inc()
        metrics.active_sessions.inc()

        return {
            "status": "created",
            "session_id": session_id,
            "stream_id": stream_id,
            "server": req.server,
            "port": req.port,
        }
    except Exception as e:
        metrics.errors_total.labels(type="session_creation").inc()
        raise HTTPException(status_code=500, detail=f"Failed to create session: {str(e)}")


@router.get("/{session_id}")
async def get_session(session_id: str):
    """Get a specific session by ID."""
    try:
        key = f"session:{session_id}"
        data = await redis_client.get(key)
        if not data:
            raise HTTPException(status_code=404, detail=f"Session {session_id} not found")
        session = json.loads(data)
        return session
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Redis error: {str(e)}")


@router.put("/{session_id}")
async def update_session(session_id: str, state: str):
    """Update session state (handshaking, connected, disconnected)."""
    try:
        key = f"session:{session_id}"
        data = await redis_client.get(key)
        if not data:
            raise HTTPException(status_code=404, detail=f"Session {session_id} not found")

        session = json.loads(data)
        session["state"] = state
        if state == "connected" and not session.get("connected_at"):
            session["connected_at"] = datetime.now(UTC).isoformat()
        elif state != "connected":
            session["connected_at"] = None

        await redis_client.setex(key, 3600, json.dumps(session))
        return {"status": "updated", "session_id": session_id, "state": state}
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Redis error: {str(e)}")


@router.delete("/{session_id}")
async def delete_session(session_id: str):
    """Delete a session and clean up state."""
    try:
        key = f"session:{session_id}"
        data = await redis_client.get(key)
        if not data:
            raise HTTPException(status_code=404, detail=f"Session {session_id} not found")

        # Delete session
        await redis_client.delete(key)

        # Clean up related metrics
        await redis_client.delete(f"metrics:{session_id}")

        metrics.active_sessions.dec()

        return {"status": "deleted", "session_id": session_id}
    except HTTPException:
        raise
    except Exception as e:
        metrics.errors_total.labels(type="session_deletion").inc()
        raise HTTPException(status_code=500, detail=f"Redis error: {str(e)}")
