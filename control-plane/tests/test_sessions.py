"""Session CRUD tests (mocked Redis via conftest)."""


def test_create_session(client):
    response = client.post("/api/sessions/", json={"server": "127.0.0.1", "port": 5000})
    assert response.status_code == 200
    data = response.json()
    assert data["status"] == "created"
    assert "session_id" in data
    assert "stream_id" in data
    assert data["server"] == "127.0.0.1"
    assert data["port"] == 5000


def test_create_session_validation(client):
    response = client.post("/api/sessions/", json={"server": "127.0.0.1"})
    assert response.status_code == 422


def test_list_sessions_empty(client):
    response = client.get("/api/sessions/")
    assert response.status_code == 200
    body = response.json()
    assert body["sessions"] == []
    assert body["count"] == 0


def test_list_sessions_includes_created(client, created_session):
    response = client.get("/api/sessions/")
    assert response.status_code == 200
    body = response.json()
    assert body["count"] >= 1
    ids = [s["session_id"] for s in body["sessions"]]
    assert created_session["session_id"] in ids


def test_get_session(client, created_session):
    sid = created_session["session_id"]
    response = client.get(f"/api/sessions/{sid}")
    assert response.status_code == 200
    data = response.json()
    assert data["session_id"] == sid
    assert data["server"] == "127.0.0.1"
    assert data["port"] == 5000
    assert data["state"] == "handshaking"


def test_get_session_not_found(client):
    response = client.get("/api/sessions/does-not-exist")
    assert response.status_code == 404


def test_update_session_state(client, created_session):
    sid = created_session["session_id"]
    response = client.put(f"/api/sessions/{sid}?state=connected")
    assert response.status_code == 200
    assert response.json()["state"] == "connected"

    got = client.get(f"/api/sessions/{sid}")
    assert got.status_code == 200
    assert got.json()["state"] == "connected"
    assert got.json().get("connected_at")


def test_update_session_not_found(client):
    response = client.put("/api/sessions/missing?state=connected")
    assert response.status_code == 404


def test_delete_session(client, created_session):
    sid = created_session["session_id"]
    response = client.delete(f"/api/sessions/{sid}")
    assert response.status_code == 200
    assert response.json()["status"] == "deleted"

    response = client.get(f"/api/sessions/{sid}")
    assert response.status_code == 404


def test_delete_session_not_found(client):
    response = client.delete("/api/sessions/missing")
    assert response.status_code == 404
