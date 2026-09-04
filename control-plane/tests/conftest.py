"""Pytest fixtures — FastAPI TestClient with in-memory Redis mock (no live Redis)."""
from __future__ import annotations

import pytest
from fastapi.testclient import TestClient
from unittest.mock import AsyncMock, MagicMock, patch

from app.main import app


@pytest.fixture
def fake_redis():
    """Async-looking Redis mock backed by a dict."""
    storage: dict[str, str] = {}
    mock = MagicMock()

    async def mock_setex(key, _ttl, value):
        storage[key] = value
        return True

    async def mock_get(key):
        return storage.get(key)

    async def mock_delete(*keys):
        deleted = 0
        for key in keys:
            if key in storage:
                del storage[key]
                deleted += 1
        return deleted

    async def mock_keys(pattern):
        if pattern.endswith("*"):
            prefix = pattern[:-1]
            return [k for k in storage if k.startswith(prefix)]
        return [k for k in storage if k == pattern]

    mock.setex = AsyncMock(side_effect=mock_setex)
    mock.get = AsyncMock(side_effect=mock_get)
    mock.delete = AsyncMock(side_effect=mock_delete)
    mock.keys = AsyncMock(side_effect=mock_keys)
    mock._storage = storage
    return mock


@pytest.fixture
def client(fake_redis):
    """TestClient with redis_client patched in every module that imports it."""
    patches = [
        patch("app.core.redis_client.redis_client", fake_redis),
        patch("app.api.sessions.redis_client", fake_redis),
        patch("app.api.metrics.redis_client", fake_redis),
        patch("app.bridge.websocket_bridge.redis_client", fake_redis),
    ]
    for p in patches:
        p.start()
    try:
        yield TestClient(app)
    finally:
        for p in patches:
            p.stop()


@pytest.fixture
def created_session(client):
    """Create one session and return the JSON body."""
    resp = client.post("/api/sessions/", json={"server": "127.0.0.1", "port": 5000})
    assert resp.status_code == 200
    return resp.json()
