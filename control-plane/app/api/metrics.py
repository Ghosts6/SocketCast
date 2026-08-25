"""Metrics endpoints — proxies/aggregates stats pulled from the C++ engine.

TODO(Phase 5/8): Prometheus scrape from the engine vs. this service
re-exposing them.
"""
from fastapi import APIRouter

router = APIRouter()


@router.get("/{session_id}")
async def get_session_metrics(session_id: str):
    # TODO: fetch jitter / RTT / loss for the session.
    return {"session_id": session_id, "jitter_ms": None, "rtt_ms": None, "loss_pct": None}
