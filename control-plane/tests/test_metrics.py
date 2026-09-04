"""Metrics endpoint tests (mocked Redis via conftest)."""


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
