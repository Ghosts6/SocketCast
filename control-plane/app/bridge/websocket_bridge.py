"""Bridges frames from the C++ engine toward a browser client.

Browsers can't open raw UDP sockets to speak the SocketCast protocol
directly, so this WebSocket endpoint proxies frames for the web dashboard's
video plane. See Doc/dev/04-architecture-and-tech-decisions.md,
"The client blind spot".

TODO(Phase 5): actually pull frames from the engine (IPC/shared queue) and
forward them here instead of the placeholder loop below.
"""
from fastapi import APIRouter, WebSocket, WebSocketDisconnect

router = APIRouter()


@router.websocket("/ws/stream/{session_id}")
async def stream_bridge(websocket: WebSocket, session_id: str):
    await websocket.accept()
    try:
        # TODO: replace with real frame forwarding from the engine.
        await websocket.send_json({"status": "not_implemented", "session_id": session_id})
    except WebSocketDisconnect:
        pass
