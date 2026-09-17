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
from app.core import metrics as prometheus_metrics

router = APIRouter()

# Active WebSocket connections per session
active_connections: dict[str, list[WebSocket]] = {}


@router.websocket("/ws/metrics/{session_id}")
async def metrics_stream(websocket: WebSocket, session_id: str):
    """Stream real-time metrics to dashboard over WebSocket."""
    await websocket.accept()

    prometheus_metrics.websocket_connections.labels(type="metrics").inc()

    session_key = f"session:{session_id}"
    session_data = await redis_client.get(session_key)
    if not session_data:
        prometheus_metrics.websocket_connections.labels(type="metrics").dec()
        await websocket.close(code=4004, reason="Session not found")
        return

    if session_id not in active_connections:
        active_connections[session_id] = []
    active_connections[session_id].append(websocket)

    engine_admin_url = f"http://{settings.engine_host}:{settings.engine_control_port}"
    prev_bytes = 0
    prev_ts = None

    try:
        session = json.loads(session_data)
        session["state"] = "connected"
        session["connected_at"] = datetime.now(UTC).isoformat()
        await redis_client.setex(session_key, 3600, json.dumps(session))

        async with httpx.AsyncClient(timeout=2.0) as client:
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
                        "stream_active": False,
                    }
                metrics.setdefault("stream_active", False)

                # Live engine stats (admin aggregate) — keep Redis overrides if present
                try:
                    resp = await client.get(f"{engine_admin_url}/admin/stats", timeout=1.0)
                    if resp.status_code == 200:
                        eng = resp.json()
                        data_sent = int(eng.get("data_sent") or 0)
                        packets = int(eng.get("packets_received") or 0)
                        frames = int(eng.get("frames_received") or 0)
                        bytes_rx = int(eng.get("bytes_received") or 0)
                        # Prefer sender counters for dashboard "live stream" view
                        if data_sent > 0 and packets == 0:
                            packets = data_sent
                        if bytes_rx == 0 and data_sent > 0:
                            # Approx until media_bytes tracked on initiator
                            bytes_rx = data_sent * 1200
                        now = datetime.now(UTC)
                        bitrate = float(eng.get("bitrate_mbps") or 0.0)
                        if prev_ts is not None and bytes_rx >= prev_bytes:
                            dt = (now - prev_ts).total_seconds()
                            if dt > 0:
                                bitrate = ((bytes_rx - prev_bytes) * 8) / (dt * 1_000_000)
                        prev_bytes = bytes_rx
                        prev_ts = now
                        metrics.update(
                            {
                                "session_id": session_id,
                                "bitrate_mbps": round(bitrate, 2),
                                "rtt_ms": round(float(eng.get("rtt_ms") or 0.0), 2),
                                "jitter_ms": round(float(eng.get("jitter_ms") or 0.0), 2),
                                "frames_received": frames,
                                "packets_received": packets,
                                "bytes_received": bytes_rx,
                                "timestamp": now.isoformat(),
                                # Lets the dashboard tell "no stream" apart from "0 = degraded".
                                "stream_active": bool(eng.get("active_streams")),
                            }
                        )
                        await redis_client.setex(metrics_key, 3600, json.dumps(metrics))
                except Exception:
                    prometheus_metrics.engine_connection_errors.inc()

                await websocket.send_json({"type": "metrics", "data": metrics})
                await asyncio.sleep(1)

    except WebSocketDisconnect:
        pass
    except Exception as e:
        prometheus_metrics.errors_total.labels(type="websocket_metrics").inc()
        print(f"WebSocket error for session {session_id}: {str(e)}")
    finally:
        prometheus_metrics.websocket_connections.labels(type="metrics").dec()

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

    prometheus_metrics.websocket_connections.labels(type="stream").inc()

    session_key = f"session:{session_id}"
    session_data = await redis_client.get(session_key)
    if not session_data:
        prometheus_metrics.websocket_connections.labels(type="stream").dec()
        await websocket.close(code=4004, reason="Session not found")
        return

    try:
        # Send ready message with stream info
        await websocket.send_json({
            "type": "frame",
            "status": "ready",
            "session_id": session_id,
            "codec": "h264",
            "format": "annex-b",
        })

        engine_admin_url = f"http://{settings.engine_host}:{settings.engine_control_port}"
        last_heartbeat = 0.0
        frame_count = 0
        # Audio is a one-shot snapshot at stream start (engine peeks the same
        # buffer every poll). Forward it once per WS connection so reconnects
        # still hear audio without re-blasting the whole clip every 50ms.
        audio_sent = False

        async with httpx.AsyncClient(timeout=2.0) as client:
            while True:
                # Client control messages (text ping)
                try:
                    message = await asyncio.wait_for(websocket.receive(), timeout=0.1)
                    if message.get("type") == "websocket.disconnect":
                        break
                    text = message.get("text")
                    if text == "ping":
                        await websocket.send_json({"type": "pong"})
                except asyncio.TimeoutError:
                    pass

                frames_data = None
                audio_data = None
                try:
                    resp = await client.get(f"{engine_admin_url}/admin/frames", timeout=1.0)
                    if resp.status_code == 200:
                        frames_data = resp.json()
                except Exception:
                    prometheus_metrics.engine_connection_errors.inc()
                    frames_data = None

                if not audio_sent:
                    try:
                        resp = await client.get(f"{engine_admin_url}/admin/audio", timeout=1.0)
                        if resp.status_code == 200:
                            audio_data = resp.json()
                    except Exception:
                        prometheus_metrics.engine_connection_errors.inc()
                        audio_data = None

                frames = (frames_data or {}).get("frames") or []
                audio_frames = (audio_data or {}).get("audio") or []
                if frames or audio_frames:
                    for frame in frames:
                        b64 = frame.get("data_base64") or ""
                        if not b64:
                            continue
                        try:
                            raw = base64.b64decode(b64)
                            if raw:
                                # Wire format: [1B type=0][8B pts_us big-endian][payload].
                                pts_us = int(frame.get("timestamp_us") or 0)
                                header = b"\x00" + pts_us.to_bytes(8, "big")
                                await websocket.send_bytes(header + raw)
                                prometheus_metrics.frames_received.inc()
                                frame_count += 1
                        except Exception:
                            continue
                    if audio_frames and not audio_sent:
                        for audio_frame in audio_frames:
                            b64 = audio_frame.get("data_base64") or ""
                            if not b64:
                                continue
                            try:
                                raw = base64.b64decode(b64)
                                if raw:
                                    # type 1 = audio
                                    pts_us = int(audio_frame.get("pts_us") or 0)
                                    header = b"\x01" + pts_us.to_bytes(8, "big")
                                    await websocket.send_bytes(header + raw)
                                    prometheus_metrics.audio_frames_received.inc()
                            except Exception:
                                continue
                        audio_sent = True
                    last_heartbeat = asyncio.get_event_loop().time()
                else:
                    # Send heartbeat if no frames
                    now = asyncio.get_event_loop().time()
                    if now - last_heartbeat >= 1.0:
                        await websocket.send_json({
                            "type": "heartbeat",
                            "frames_sent": frame_count,
                        })
                        last_heartbeat = now

                # Delay to let frame buffer accumulate (was 10ms, causing drain faster than fill)
                await asyncio.sleep(0.05)

    except WebSocketDisconnect:
        pass
    except Exception as e:
        prometheus_metrics.errors_total.labels(type="websocket_stream").inc()
        print(f"Stream bridge error for session {session_id}: {str(e)}")
    finally:
        prometheus_metrics.websocket_connections.labels(type="stream").dec()
