"""End-to-end workflows on TestClient + mocked Redis (no live Redis required).

Named integration historically; uses the same fake Redis as unit tests.
"""


def test_session_creation_and_retrieval(client):
    create_resp = client.post("/api/sessions/", json={"server": "127.0.0.1", "port": 5000})
    assert create_resp.status_code == 200
    session_id = create_resp.json()["session_id"]
    assert session_id

    list_resp = client.get("/api/sessions/")
    assert list_resp.status_code == 200
    assert list_resp.json()["count"] >= 1

    get_resp = client.get(f"/api/sessions/{session_id}")
    assert get_resp.status_code == 200
    assert get_resp.json()["session_id"] == session_id

    update_resp = client.put(f"/api/sessions/{session_id}?state=connected")
    assert update_resp.status_code == 200

    delete_resp = client.delete(f"/api/sessions/{session_id}")
    assert delete_resp.status_code == 200

    missing = client.get(f"/api/sessions/{session_id}")
    assert missing.status_code == 404


def test_metrics_workflow(client):
    create_resp = client.post("/api/sessions/", json={"server": "127.0.0.1", "port": 5000})
    assert create_resp.status_code == 200
    session_id = create_resp.json()["session_id"]

    metrics_resp = client.get(f"/api/metrics/{session_id}")
    assert metrics_resp.status_code == 200
    assert metrics_resp.json()["bitrate_mbps"] == 0.0

    post_resp = client.post(
        f"/api/metrics/{session_id}",
        json={"bitrate_mbps": 2.5, "rtt_ms": 25.0},
    )
    assert post_resp.status_code == 200

    updated_resp = client.get(f"/api/metrics/{session_id}")
    assert updated_resp.status_code == 200
    updated = updated_resp.json()
    assert updated["bitrate_mbps"] == 2.5
    assert updated["rtt_ms"] == 25.0


def test_cors_headers_present(client):
    response = client.options(
        "/api/sessions/",
        headers={
            "Origin": "http://localhost:5173",
            "Access-Control-Request-Method": "POST",
        },
    )
    # With allow_credentials=True, middleware echoes the request Origin.
    assert response.status_code in (200, 204)
    assert response.headers.get("access-control-allow-origin") in (
        "*",
        "http://localhost:5173",
    )
