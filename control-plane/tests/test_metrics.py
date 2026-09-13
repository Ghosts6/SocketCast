"""Metrics endpoint tests (mocked Redis via conftest)."""
import base64
import re
from unittest.mock import AsyncMock, MagicMock, patch


def test_prometheus_metrics_endpoint_exposes_audio_counter(client, created_session):
    """/metrics (Prometheus scrape) reflects audio frames forwarded over /ws/stream."""
    sid = created_session["session_id"]

    def counter_value(body: str) -> float:
        match = re.search(r'^socketcast_audio_frames_received_total\s+([\d.e+-]+)$', body,
                          re.MULTILINE)
        return float(match.group(1)) if match else 0.0

    before = counter_value(client.get("/metrics").text)

    audio_raw = bytes([0xFF, 0xF1, 0x4C, 0x80, 0x00, 0x1F, 0xFC])
    audio_resp = MagicMock(status_code=200)
    audio_resp.json.return_value = {
        "audio": [{"pts_us": 0, "sample_rate": 48000, "channels": 2,
                   "data_base64": base64.b64encode(audio_raw).decode("ascii")}]
    }
    video_resp = MagicMock(status_code=200)
    video_resp.json.return_value = {"frames": []}

    async def fake_get(url, timeout=1.0):
        return audio_resp if url.endswith("/admin/audio") else video_resp

    mock_client = MagicMock()
    mock_client.__aenter__ = AsyncMock(return_value=mock_client)
    mock_client.__aexit__ = AsyncMock(return_value=None)
    mock_client.get = fake_get

    with patch("app.bridge.websocket_bridge.httpx.AsyncClient", return_value=mock_client):
        with client.websocket_connect(f"/ws/stream/{sid}") as ws:
            ws.receive_json()  # ready
            for _ in range(8):
                data = ws.receive()
                if "bytes" in data and data["bytes"][0] == 0x01:  # audio type byte
                    break

    after = counter_value(client.get("/metrics").text)
    assert after >= before + 1


def test_get_metrics_defaults(client, created_session):
    sid = created_session["session_id"]
    response = client.get(f"/api/metrics/{sid}")
    assert response.status_code == 200
    data = response.json()
    assert data["session_id"] == sid
    assert data["bitrate_mbps"] == 0.0
    assert data["rtt_ms"] == 0.0
    assert data["frames_received"] == 0


def test_get_metrics_session_missing(client):
    response = client.get("/api/metrics/no-such-session")
    assert response.status_code == 404


def test_post_and_get_metrics(client, created_session):
    sid = created_session["session_id"]
    payload = {
        "session_id": sid,
        "bitrate_mbps": 2.5,
        "rtt_ms": 25.0,
        "jitter_ms": 4.0,
        "loss_percent": 0.1,
        "frames_received": 10,
        "packets_received": 100,
        "bytes_received": 50000,
    }
    post = client.post(f"/api/metrics/{sid}", json=payload)
    assert post.status_code == 200
    assert post.json()["status"] == "updated"

    get = client.get(f"/api/metrics/{sid}")
    assert get.status_code == 200
    data = get.json()
    assert data["bitrate_mbps"] == 2.5
    assert data["rtt_ms"] == 25.0
    assert data["frames_received"] == 10


def test_post_metrics_session_missing(client):
    response = client.post("/api/metrics/missing", json={"bitrate_mbps": 1.0})
    assert response.status_code == 404
