"""Middleware for request tracking and metrics."""
import time
from fastapi import Request
from starlette.middleware.base import BaseHTTPMiddleware

from app.core import metrics


class MetricsMiddleware(BaseHTTPMiddleware):
    async def dispatch(self, request: Request, call_next):
        start_time = time.time()
        try:
            response = await call_next(request)
            status_code = response.status_code
        except Exception as e:
            metrics.errors_total.labels(type="request_error").inc()
            raise

        duration = time.time() - start_time

        metrics.request_count.labels(
            method=request.method,
            endpoint=request.url.path,
            status=status_code,
        ).inc()

        metrics.request_duration.labels(
            method=request.method,
            endpoint=request.url.path,
        ).observe(duration)

        return response
