"""WebSocket bridge tests (mocked Redis via conftest)."""


def test_metrics_ws_unknown_session(client):
    with client.websocket_connect("/ws/metrics/unknown-id") as ws:
        # Server accepts then closes with 4004 — Starlette raises on receive.
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


def test_stream_ws_stub_and_ping(client, created_session):
    """Verify frame WS is Phase 5b (engine IPC in progress)."""
    sid = created_session["session_id"]
    with client.websocket_connect(f"/ws/stream/{sid}") as ws:
        msg = ws.receive_json()
        assert msg["type"] == "frame"
        assert msg["status"] == "ready", "frame WS ready for Phase 5b streaming"
        assert msg["session_id"] == sid
        assert "Phase 5b" in msg["note"]

        ws.send_text("ping")
        pong = ws.receive_json()
        assert pong["type"] == "pong"


def test_stream_ws_unknown_session(client):
    with client.websocket_connect("/ws/stream/unknown-id") as ws:
        try:
            ws.receive_json()
            closed_cleanly = False
        except Exception:
            closed_cleanly = True
        assert closed_cleanly
