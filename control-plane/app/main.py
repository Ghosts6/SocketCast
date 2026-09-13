"""SocketCast control-plane entrypoint."""
import json
import logging
import sys
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from prometheus_client import generate_latest, CONTENT_TYPE_LATEST
from starlette.responses import Response

from app.api import admin, metrics, sessions
from app.bridge import websocket_bridge
from app.core.config import settings
from app.core import metrics as prometheus_metrics
from app.core.middleware import MetricsMiddleware
from app.core.redis_client import redis_client

# Structured JSON logging
class JSONFormatter(logging.Formatter):
    def format(self, record):
        log_data = {
            "timestamp": self.formatTime(record),
            "level": record.levelname,
            "logger": record.name,
            "message": record.getMessage(),
        }
        if record.exc_info:
            log_data["exception"] = self.formatException(record.exc_info)
        return json.dumps(log_data)

handler = logging.StreamHandler(sys.stdout)
handler.setFormatter(JSONFormatter())
logging.basicConfig(level=settings.socketcast_log_level.upper(), handlers=[handler])
logger = logging.getLogger("socketcast.control-plane")

# Validate configuration on startup
try:
    logger.info(f"Starting SocketCast Control Plane. Redis: {settings.redis_url}")
    logger.info(f"Engine: {settings.engine_host}:{settings.engine_control_port}")
except Exception as e:
    logger.error(f"Configuration error: {e}")
    raise

app = FastAPI(title="SocketCast Control Plane")

app.add_middleware(MetricsMiddleware)

# Enable CORS for dashboard
allowed_origins = settings.allowed_origins.split(",") if settings.allowed_origins else ["*"]
app.add_middleware(
    CORSMiddleware,
    allow_origins=allowed_origins,
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
    """Health check endpoint."""
    try:
        # Check Redis connectivity
        await redis_client.ping()
        return {"status": "ok", "redis": "connected", "version": "1.0.0"}
    except Exception as e:
        logger.error(f"Health check failed: {e}")
        return JSONResponse(status_code=503, content={"status": "degraded", "error": str(e)})

@app.get("/metrics", response_class=Response)
async def prometheus_metrics():
    """Prometheus metrics endpoint."""
    return Response(generate_latest(), media_type=CONTENT_TYPE_LATEST)
