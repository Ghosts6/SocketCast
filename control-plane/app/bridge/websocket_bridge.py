"""WebSocket bridge for real-time metrics & frame streaming to dashboard."""
import asyncio
import json
import random
from datetime import datetime, UTC
from fastapi import APIRouter, WebSocket, WebSocketDisconnect, HTTPException

from app.core.redis_client import redis_client
from app.core.config import settings

router = APIRouter()

# Active WebSocket connections per session
active_connections: dict[str, list[WebSocket]] = {}


@router.websocket("/ws/metrics/{session_id}")
async def metrics_stream(websocket: WebSocket, session_id: str):
    """Stream real-time metrics to dashboard over WebSocket."""
    await websocket.accept()

    # Check session exists
    session_key = f"session:{session_id}"
    session_data = await redis_client.get(session_key)
    if not session_data:
        await websocket.close(code=4004, reason="Session not found")
        return

    # Register connection
    if session_id not in active_connections:
        active_connections[session_id] = []
    active_connections[session_id].append(websocket)

    try:
        # Update session state to connected
        session = json.loads(session_data)
        session["state"] = "connected"
        session["connected_at"] = datetime.now(UTC).isoformat()
        await redis_client.setex(session_key, 3600, json.dumps(session))

        # Stream metrics every second
        while True:
            metrics_key = f"metrics:{session_id}"
            metrics_data = await redis_client.get(metrics_key)

            if metrics_data:
                metrics = json.loads(metrics_data)
            elif settings.socketcast_mock_metrics:
                # Generate mock metrics only if explicitly enabled (demo/testing)
                metrics = {
                    "session_id": session_id,
                    "bitrate_mbps": round(random.uniform(0.5, 5.0), 2),
                    "rtt_ms": round(random.uniform(5, 50), 2),
                    "jitter_ms": round(random.uniform(0, 10), 2),
                    "loss_percent": round(random.uniform(0, 2), 2),
                    "frames_received": random.randint(100, 500),
                    "packets_received": random.randint(1000, 5000),
                    "bytes_received": random.randint(100000, 500000),
                    "timestamp": datetime.now(UTC).isoformat(),
                }
            else:
                # No metrics available and mocking disabled
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

            await websocket.send_json({"type": "metrics", "data": metrics})
            await asyncio.sleep(1)

    except WebSocketDisconnect:
        pass
    except Exception as e:
        print(f"WebSocket error for session {session_id}: {str(e)}")
    finally:
        # Unregister connection
        if session_id in active_connections:
            try:
                active_connections[session_id].remove(websocket)
            except ValueError:
                pass

        # Update session state to disconnected
        try:
            session_data = await redis_client.get(session_key)
            if session_data:
                session = json.loads(session_data)
                session["state"] = "disconnected"
                await redis_client.setex(session_key, 3600, json.dumps(session))
        except Exception:
            pass


@router.websocket("/ws/stream/{session_id}")
async def stream_bridge(websocket: WebSocket, session_id: str):
    """Placeholder for Phase 5b (engine IPC not yet built).

    This socket does NOT forward H.264 frames in Phase 5a. It exists as a
    stub to allow the dashboard to attempt a connection gracefully. Real frame
    forwarding requires an engine control channel to extract live streams —
    deferred to Phase 5b.
    """
    await websocket.accept()

    # Check session exists
    session_key = f"session:{session_id}"
    session_data = await redis_client.get(session_key)
    if not session_data:
        await websocket.close(code=4004, reason="Session not found")
        return

    try:
        await websocket.send_json({
            "type": "frame",
            "status": "stub",
            "session_id": session_id,
            "note": "Frame forwarding deferred to Phase 5b (engine IPC) — no frame data in Phase 5a"
        })

        # Keep connection alive (ping/pong only — no frames)
        while True:
            data = await websocket.receive_text()
            if data == "ping":
                await websocket.send_json({"type": "pong"})

    except WebSocketDisconnect:
        pass
    except Exception as e:
        print(f"Stream bridge error for session {session_id}: {str(e)}")
