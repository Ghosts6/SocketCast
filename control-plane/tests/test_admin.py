"""Admin API proxy tests (engine HTTP mocked via httpx)."""
from unittest.mock import AsyncMock, MagicMock, patch

import httpx
import pytest


@pytest.fixture
def mock_engine():
    """Patch httpx.AsyncClient used by admin routes."""
    client = MagicMock()
    client.__aenter__ = AsyncMock(return_value=client)
    client.__aexit__ = AsyncMock(return_value=None)
    with patch("app.api.admin.httpx.AsyncClient", return_value=client):
        yield client


def test_start_stream_proxies_to_engine(client, mock_engine):
    resp_mock = MagicMock()
    resp_mock.status_code = 200
    resp_mock.json.return_value = {"stream_id": "stream_9"}
    mock_engine.post = AsyncMock(return_value=resp_mock)

    r = client.post(
        "/api/admin/streams",
        json={"input": "/tmp/sc_test.h264", "host": "127.0.0.1", "port": 5000, "fps": 10},
    )
    assert r.status_code == 200
    assert r.json()["stream_id"] == "stream_9"
    mock_engine.post.assert_awaited()
    args, kwargs = mock_engine.post.await_args
    assert args[0].endswith("/admin/streams")
    assert kwargs["json"]["input"] == "/tmp/sc_test.h264"


def test_start_stream_engine_down(client, mock_engine):
    mock_engine.post = AsyncMock(side_effect=httpx.ConnectError("boom"))
    r = client.post(
        "/api/admin/streams",
        json={"input": "/tmp/sc_test.h264", "host": "127.0.0.1", "port": 5000, "fps": 10},
    )
    assert r.status_code == 503


def test_start_stream_engine_400(client, mock_engine):
    resp_mock = MagicMock()
    resp_mock.status_code = 400
    resp_mock.json.return_value = {"error": "missing input"}
    mock_engine.post = AsyncMock(return_value=resp_mock)

    r = client.post(
        "/api/admin/streams",
        json={"input": "/tmp/sc_test.h264", "host": "127.0.0.1", "port": 5000, "fps": 10},
    )
    assert r.status_code == 400


def test_get_stats_and_stop(client, mock_engine):
    stats = MagicMock()
    stats.status_code = 200
    stats.json.return_value = {"rtt_ms": 1.5, "data_sent": 12}
    mock_engine.get = AsyncMock(return_value=stats)

    r = client.get("/api/admin/streams/stream_1/stats")
    assert r.status_code == 200
    assert r.json()["data_sent"] == 12

    stopped = MagicMock()
    stopped.status_code = 200
    stopped.json.return_value = {"status": "stopped"}
    mock_engine.delete = AsyncMock(return_value=stopped)
    r = client.delete("/api/admin/streams/stream_1")
    assert r.status_code == 200
    assert r.json()["status"] == "stopped"
