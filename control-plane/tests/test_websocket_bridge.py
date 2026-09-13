"""WebSocket bridge tests (mocked Redis via conftest)."""
import base64
from unittest.mock import AsyncMock, MagicMock, patch


def frame_header(type_byte: int, pts_us: int) -> bytes:
    """Build the [1B type][8B pts_us big-endian] prefix the bridge sends before each frame."""
    return bytes([type_byte]) + pts_us.to_bytes(8, "big")


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

    # Engine unreachable in this test — posted Redis metrics must pass through
    # untouched rather than depend on whatever happens to listen on port 5001.
    mock_client = MagicMock()
    mock_client.__aenter__ = AsyncMock(return_value=mock_client)
    mock_client.__aexit__ = AsyncMock(return_value=None)
    mock_client.get = AsyncMock(side_effect=ConnectionError("engine unreachable"))

    with patch("app.bridge.websocket_bridge.httpx.AsyncClient", return_value=mock_client):
        with client.websocket_connect(f"/ws/metrics/{sid}") as ws:
            message = ws.receive_json()
    assert message["type"] == "metrics"
    assert "data" in message
    assert message["data"]["bitrate_mbps"] == 1.25
    assert message["data"]["session_id"] == sid


def test_stream_ws_ready_ping_and_binary(client, created_session):
    """Ready JSON, ping/pong, then a [type][pts_us][Annex B] binary frame."""
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
            assert msg["codec"] == "h264"
            assert msg["format"] == "annex-b"

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
                    assert data["bytes"] == frame_header(0x00, 1) + raw
                    saw_binary = True
                if saw_pong and saw_binary:
                    break
            assert saw_pong
            assert saw_binary


def test_stream_ws_audio_frames_are_type_and_pts_prefixed(client, created_session):
    """Audio frames from /admin/audio arrive as [0x01][pts_us][ADTS] binary frames."""
    sid = created_session["session_id"]
    video_raw = bytes([0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1E])
    audio_raw = bytes([0xFF, 0xF1, 0x4C, 0x80, 0x00, 0x1F, 0xFC])

    video_resp = MagicMock(status_code=200)
    video_resp.json.return_value = {
        "frames": [{"timestamp_us": 1, "is_keyframe": True,
                    "data_base64": base64.b64encode(video_raw).decode("ascii")}]
    }
    audio_resp = MagicMock(status_code=200)
    audio_resp.json.return_value = {
        "audio": [{"pts_us": 21333, "sample_rate": 48000, "channels": 2,
                   "data_base64": base64.b64encode(audio_raw).decode("ascii")}]
    }

    async def fake_get(url, timeout=1.0):
        return audio_resp if url.endswith("/admin/audio") else video_resp

    mock_client = MagicMock()
    mock_client.__aenter__ = AsyncMock(return_value=mock_client)
    mock_client.__aexit__ = AsyncMock(return_value=None)
    mock_client.get = fake_get

    expected_video = frame_header(0x00, 1) + video_raw
    expected_audio = frame_header(0x01, 21333) + audio_raw

    with patch("app.bridge.websocket_bridge.httpx.AsyncClient", return_value=mock_client):
        with client.websocket_connect(f"/ws/stream/{sid}") as ws:
            ws.receive_json()  # ready

            saw_video = False
            saw_audio = False
            for _ in range(8):
                data = ws.receive()
                if "bytes" not in data:
                    continue
                payload = data["bytes"]
                if payload == expected_video:
                    saw_video = True
                elif payload == expected_audio:
                    saw_audio = True
                if saw_video and saw_audio:
                    break
            assert saw_video
            assert saw_audio


def test_stream_ws_unknown_session(client):
    with client.websocket_connect("/ws/stream/unknown-id") as ws:
        try:
            ws.receive_json()
            closed_cleanly = False
        except Exception:
            closed_cleanly = True
        assert closed_cleanly
