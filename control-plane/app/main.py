"""SocketCast control-plane entrypoint (Phase 5).

Session APIs and the browser bridge live here; protocol logic stays in C++.
"""
from fastapi import FastAPI

from app.api import sessions, metrics
from app.bridge import websocket_bridge

app = FastAPI(title="SocketCast Control Plane")

app.include_router(sessions.router, prefix="/api/sessions", tags=["sessions"])
app.include_router(metrics.router, prefix="/api/metrics", tags=["metrics"])
app.include_router(websocket_bridge.router, tags=["bridge"])


@app.get("/healthz")
async def healthz():
    return {"status": "ok"}
