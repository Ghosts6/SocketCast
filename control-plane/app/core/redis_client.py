"""Shared Redis client for session state and rate-limit counters."""
import redis.asyncio as redis

from app.core.config import settings

redis_client = redis.from_url(settings.redis_url, decode_responses=True)
