"""WebSocket bridge for real-time metrics & frame streaming to dashboard."""
import asyncio
import base64
import json
import random
from datetime import datetime, UTC

import httpx
from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from app.core.redis_client import redis_client
from app.core.config import settings

router = APIRouter()

# Active WebSocket connections per session
active_connections: dict[str, list[WebSocket]] = {}


@router.websocket("/ws/metrics/{session_id}")
async def metrics_stream(websocket: WebSocket, session_id: str):
    """Stream real-time metrics to dashboard over WebSocket."""
    await websocket.accept()

    session_key = f"session:{session_id}"
    session_data = await redis_client.get(session_key)
    if not session_data:
        await websocket.close(code=4004, reason="Session not found")
        return

    if session_id not in active_connections:
        active_connections[session_id] = []
    active_connections[session_id].append(websocket)

    try:
        session = json.loads(session_data)
        session["state"] = "connected"
        session["connected_at"] = datetime.now(UTC).isoformat()
        await redis_client.setex(session_key, 3600, json.dumps(session))

        while True:
            metrics_key = f"metrics:{session_id}"
            metrics_data = await redis_client.get(metrics_key)

            if metrics_data:
                metrics = json.loads(metrics_data)
            elif settings.socketcast_mock_metrics:
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
        if session_id in active_connections:
            try:
                active_connections[session_id].remove(websocket)
            except ValueError:
                pass

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
    """Stream H.264 Annex B NALs to the dashboard as binary WebSocket frames."""
    await websocket.accept()

    session_key = f"session:{session_id}"
    session_data = await redis_client.get(session_key)
    if not session_data:
        await websocket.close(code=4004, reason="Session not found")
        return

    try:
        await websocket.send_json({
            "type": "frame",
            "status": "ready",
            "session_id": session_id,
            "note": "Binary H.264 Annex B frames from engine",
        })

        engine_admin_url = f"http://{settings.engine_host}:{settings.engine_control_port}"
        last_heartbeat = 0.0

        async with httpx.AsyncClient(timeout=1.0) as client:
            while True:
                # Client control messages (text ping)
                try:
                    message = await asyncio.wait_for(websocket.receive(), timeout=0.05)
                    if message.get("type") == "websocket.disconnect":
                        break
                    text = message.get("text")
                    if text == "ping":
                        await websocket.send_json({"type": "pong"})
                except asyncio.TimeoutError:
                    pass

                frames_data = None
                try:
                    resp = await client.get(f"{engine_admin_url}/admin/frames")
                    if resp.status_code == 200:
                        frames_data = resp.json()
                except Exception:
                    frames_data = None

                frames = (frames_data or {}).get("frames") or []
                if frames:
                    for frame in frames:
                        b64 = frame.get("data_base64") or ""
                        if not b64:
                            continue
                        try:
                            raw = base64.b64decode(b64)
                        except Exception:
                            continue
                        if raw:
                            await websocket.send_bytes(raw)
                else:
                    now = asyncio.get_event_loop().time()
                    if now - last_heartbeat >= 1.0:
                        await websocket.send_json({"type": "heartbeat"})
                        last_heartbeat = now

    except WebSocketDisconnect:
        pass
    except Exception as e:
        print(f"Stream bridge error for session {session_id}: {str(e)}")
