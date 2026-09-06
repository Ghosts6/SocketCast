"""SocketCast control-plane entrypoint (Phase 5).

Session APIs and the browser bridge live here; protocol logic stays in C++.
"""
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from app.api import admin, metrics, sessions
from app.bridge import websocket_bridge

app = FastAPI(title="SocketCast Control Plane")

# Enable CORS for dashboard
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # TODO: restrict to dashboard domain in production
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(sessions.router, prefix="/api/sessions", tags=["sessions"])
app.include_router(metrics.router, prefix="/api/metrics", tags=["metrics"])
app.include_router(admin.router, prefix="/api/admin", tags=["admin"])
app.include_router(websocket_bridge.router, tags=["bridge"])


@app.get("/healthz")
async def healthz():
    return {"status": "ok"}
