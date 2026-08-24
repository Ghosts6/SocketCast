"""SocketCast control-plane entrypoint.

See Doc/dev/03-roadmap-and-scope.md, Phase 5, and
Doc/dev/04-architecture-and-tech-decisions.md for the C++/Python boundary.
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
