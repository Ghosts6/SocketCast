"""Metrics endpoints — real-time stats from sessions."""
import json
from datetime import datetime, UTC
from fastapi import APIRouter, HTTPException

from app.core.redis_client import redis_client

router = APIRouter()


@router.get("/{session_id}")
async def get_session_metrics(session_id: str):
    """Get current metrics for a session."""
    try:
        # Check session exists
        session_key = f"session:{session_id}"
        session_data = await redis_client.get(session_key)
        if not session_data:
            raise HTTPException(status_code=404, detail=f"Session {session_id} not found")

        # Get or create metrics
        metrics_key = f"metrics:{session_id}"
        metrics_data = await redis_client.get(metrics_key)

        if metrics_data:
            metrics = json.loads(metrics_data)
        else:
            # Create empty metrics dict (all strings for JSON compatibility)
            metrics = {
                "session_id": session_id,
                "bitrate_mbps": 0.0,
                "rtt_ms": 0.0,
                "jitter_ms": 0.0,
                "loss_percent": 0.0,
                "frames_received": 0,
                "packets_received": 0,
                "bytes_received": 0,
                "timestamp": datetime.now(UTC).isoformat(),
            }
            await redis_client.setex(metrics_key, 3600, json.dumps(metrics))

        return metrics
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Redis error: {str(e)}")


@router.post("/{session_id}")
async def update_session_metrics(session_id: str, metrics: dict):
    """Update metrics for a session (called by WebSocket bridge or engine)."""
    try:
        # Check session exists
        session_key = f"session:{session_id}"
        session_data = await redis_client.get(session_key)
        if not session_data:
            raise HTTPException(status_code=404, detail=f"Session {session_id} not found")

        # Store metrics with 10min TTL (live metric expiry)
        metrics_key = f"metrics:{session_id}"
        await redis_client.setex(metrics_key, 600, json.dumps(metrics))
        return {"status": "updated", "session_id": session_id}
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Redis error: {str(e)}")
