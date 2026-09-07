"""WebSocket bridge tests (mocked Redis via conftest)."""
import base64
from unittest.mock import AsyncMock, MagicMock, patch


def test_metrics_ws_unknown_session(client):
    with client.websocket_connect("/ws/metrics/unknown-id") as ws:
        try:
            ws.receive_json()
            closed_cleanly = False
        except Exception:
            closed_cleanly = True
        assert closed_cleanly


def test_metrics_ws_streams_payload(client, created_session):
    sid = created_session["session_id"]
    client.post(
        f"/api/metrics/{sid}",
        json={
            "session_id": sid,
            "bitrate_mbps": 1.25,
            "rtt_ms": 12.0,
            "jitter_ms": 1.0,
            "loss_percent": 0.0,
            "frames_received": 3,
            "packets_received": 30,
            "bytes_received": 9000,
        },
    )

    with client.websocket_connect(f"/ws/metrics/{sid}") as ws:
        message = ws.receive_json()
        assert message["type"] == "metrics"
        assert "data" in message
        assert message["data"]["bitrate_mbps"] == 1.25
        assert message["data"]["session_id"] == sid


def test_stream_ws_ready_ping_and_binary(client, created_session):
    """Ready JSON, ping/pong, then binary Annex B when engine has frames."""
    sid = created_session["session_id"]
    raw = bytes([0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1E])

    mock_resp = MagicMock()
    mock_resp.status_code = 200
    mock_resp.json.return_value = {
        "frames": [
            {
                "timestamp_us": 1,
                "is_keyframe": True,
                "data_base64": base64.b64encode(raw).decode("ascii"),
            }
        ]
    }
    mock_client = MagicMock()
    mock_client.__aenter__ = AsyncMock(return_value=mock_client)
    mock_client.__aexit__ = AsyncMock(return_value=None)
    mock_client.get = AsyncMock(return_value=mock_resp)

    with patch("app.bridge.websocket_bridge.httpx.AsyncClient", return_value=mock_client):
        with client.websocket_connect(f"/ws/stream/{sid}") as ws:
            msg = ws.receive_json()
            assert msg["type"] == "frame"
            assert msg["status"] == "ready"
            assert msg["session_id"] == sid
            assert "binary" in msg["note"].lower() or "h.264" in msg["note"].lower()

            ws.send_text("ping")
            # May receive binary and/or pong; drain until pong
            saw_pong = False
            saw_binary = False
            for _ in range(8):
                data = ws.receive()
                if "text" in data:
                    import json

                    payload = json.loads(data["text"])
                    if payload.get("type") == "pong":
                        saw_pong = True
                    if payload.get("type") == "heartbeat":
                        continue
                elif "bytes" in data:
                    assert data["bytes"] == raw
                    saw_binary = True
                if saw_pong and saw_binary:
                    break
            assert saw_pong
            assert saw_binary


def test_stream_ws_unknown_session(client):
    with client.websocket_connect("/ws/stream/unknown-id") as ws:
        try:
            ws.receive_json()
            closed_cleanly = False
        except Exception:
            closed_cleanly = True
        assert closed_cleanly
