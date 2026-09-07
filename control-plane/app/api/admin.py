"""Admin endpoints that proxy to the engine HTTP control server."""
from typing import Any

import httpx
from fastapi import APIRouter, HTTPException
from pydantic import BaseModel

from app.core.config import settings

router = APIRouter()


def _engine_base() -> str:
    return f"http://{settings.engine_host}:{settings.engine_control_port}"


class StreamRequest(BaseModel):
    input: str
    host: str = "127.0.0.1"
    port: int = 5000
    fps: int = 30


class StreamResponse(BaseModel):
    stream_id: str


@router.post("/streams")
async def start_stream(req: StreamRequest) -> StreamResponse:
    """Start a media stream on the engine."""
    try:
        async with httpx.AsyncClient(timeout=5.0) as client:
            resp = await client.post(
                f"{_engine_base()}/admin/streams",
                json=req.model_dump(),
            )
        if resp.status_code == 400:
            raise HTTPException(status_code=400, detail=resp.json().get("error", "bad request"))
        if resp.status_code >= 400:
            raise HTTPException(status_code=resp.status_code, detail=resp.text)
        body = resp.json()
        if "stream_id" not in body:
            raise HTTPException(status_code=500, detail="No stream_id in engine response")
        return StreamResponse(stream_id=body["stream_id"])
    except httpx.HTTPError as e:
        raise HTTPException(status_code=503, detail=f"Engine admin server unavailable: {e}") from e


@router.delete("/streams/{stream_id}")
async def stop_stream(stream_id: str) -> dict[str, str]:
    """Stop a media stream on the engine."""
    try:
        async with httpx.AsyncClient(timeout=5.0) as client:
            resp = await client.delete(f"{_engine_base()}/admin/streams/{stream_id}")
        if resp.status_code == 404:
            raise HTTPException(status_code=404, detail="Stream not found")
        if resp.status_code >= 400:
            raise HTTPException(status_code=resp.status_code, detail=resp.text)
        return resp.json()
    except httpx.HTTPError as e:
        raise HTTPException(status_code=503, detail=f"Engine admin server unavailable: {e}") from e


@router.get("/streams/{stream_id}/stats")
async def get_stream_stats(stream_id: str) -> dict[str, Any]:
    """Get stats for a media stream."""
    try:
        async with httpx.AsyncClient(timeout=5.0) as client:
            resp = await client.get(f"{_engine_base()}/admin/streams/{stream_id}/stats")
        if resp.status_code == 404:
            raise HTTPException(status_code=404, detail="Stream not found")
        if resp.status_code >= 400:
            raise HTTPException(status_code=resp.status_code, detail=resp.text)
        return resp.json()
    except httpx.HTTPError as e:
        raise HTTPException(status_code=503, detail=f"Engine admin server unavailable: {e}") from e
