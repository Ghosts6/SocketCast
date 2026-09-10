"""Health endpoint."""
from unittest.mock import AsyncMock, patch


def test_healthz(client):
    response = client.get("/healthz")
    assert response.status_code == 200
    body = response.json()
    assert body["status"] == "ok"
    assert body["redis"] == "connected"


def test_healthz_degraded_when_redis_down(client):
    with patch("app.main.redis_client.ping", new=AsyncMock(side_effect=Exception("down"))):
        response = client.get("/healthz")
    assert response.status_code == 503
    assert response.json()["status"] == "degraded"
