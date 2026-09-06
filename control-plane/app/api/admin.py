"""Admin endpoints for Phase 5b engine control (stream start/stop/stats)."""
import json
import urllib.request
from typing import Any

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel

from app.core.config import settings

router = APIRouter()

ENGINE_ADMIN_URL = "http://127.0.0.1:5001"  # Phase 5b: engine admin server


class StreamRequest(BaseModel):
    input: str
    host: str = "127.0.0.1"
    port: int = 5000
    fps: int = 30


class StreamResponse(BaseModel):
    stream_id: str


@router.post("/streams")
async def start_stream(req: StreamRequest) -> StreamResponse:
    """Start a media stream on the engine (Phase 5b)."""
    try:
        data = json.dumps({
            "input": req.input,
            "host": req.host,
            "port": req.port,
            "fps": req.fps,
        }).encode("utf-8")

        http_req = urllib.request.Request(
            f"{ENGINE_ADMIN_URL}/admin/streams",
            data=data,
            headers={"Content-Type": "application/json"},
            method="POST",
        )

        with urllib.request.urlopen(http_req, timeout=5) as response:
            body = json.loads(response.read().decode("utf-8"))
            if "stream_id" in body:
                return StreamResponse(stream_id=body["stream_id"])
            raise ValueError("No stream_id in response")
    except urllib.error.URLError as e:
        raise HTTPException(status_code=503, detail=f"Engine admin server unavailable: {e}")
    except (json.JSONDecodeError, ValueError) as e:
        raise HTTPException(status_code=500, detail=f"Invalid response from engine: {e}")


@router.delete("/streams/{stream_id}")
async def stop_stream(stream_id: str) -> dict[str, str]:
    """Stop a media stream on the engine (Phase 5b)."""
    try:
        http_req = urllib.request.Request(
            f"{ENGINE_ADMIN_URL}/admin/streams/{stream_id}",
            method="DELETE",
        )

        with urllib.request.urlopen(http_req, timeout=5) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        if e.code == 404:
            raise HTTPException(status_code=404, detail="Stream not found")
        raise HTTPException(status_code=e.code, detail=str(e))
    except urllib.error.URLError as e:
        raise HTTPException(status_code=503, detail=f"Engine admin server unavailable: {e}")


@router.get("/streams/{stream_id}/stats")
async def get_stream_stats(stream_id: str) -> dict[str, Any]:
    """Get stats for a media stream (Phase 5b)."""
    try:
        http_req = urllib.request.Request(
            f"{ENGINE_ADMIN_URL}/admin/streams/{stream_id}/stats",
        )

        with urllib.request.urlopen(http_req, timeout=5) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        if e.code == 404:
            raise HTTPException(status_code=404, detail="Stream not found")
        raise HTTPException(status_code=e.code, detail=str(e))
    except urllib.error.URLError as e:
        raise HTTPException(status_code=503, detail=f"Engine admin server unavailable: {e}")
