"""Metrics endpoints — proxies/aggregates stats pulled from the C++ engine.

TODO(Phase 5/8): decide the exact boundary (Prometheus scrape from engine
vs. this service re-exposing them) — see
Doc/dev/04-architecture-and-tech-decisions.md, "Observability boundary".
"""
from fastapi import APIRouter

router = APIRouter()


@router.get("/{session_id}")
async def get_session_metrics(session_id: str):
    # TODO: fetch jitter / RTT / loss for the session.
    return {"session_id": session_id, "jitter_ms": None, "rtt_ms": None, "loss_pct": None}
