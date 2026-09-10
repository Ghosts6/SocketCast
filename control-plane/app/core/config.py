"""App configuration loaded from environment (.env in development)."""
from pydantic import field_validator
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file=".env")

    socketcast_env: str = "development"
    socketcast_log_level: str = "info"
    redis_url: str = "redis://localhost:6379/0"
    engine_host: str = "localhost"
    engine_control_port: int = 5001
    socketcast_mock_metrics: bool = False
    allowed_origins: str = "*"  # CSV of allowed origins for CORS

    @field_validator("socketcast_log_level")
    @classmethod
    def validate_log_level(cls, v: str) -> str:
        valid = {"debug", "info", "warning", "error", "critical"}
        if v.lower() not in valid:
            raise ValueError(f"Invalid log level: {v}. Must be one of {valid}")
        return v.lower()

    @field_validator("redis_url")
    @classmethod
    def validate_redis_url(cls, v: str) -> str:
        if not v.startswith("redis://"):
            raise ValueError("REDIS_URL must start with redis://")
        return v


settings = Settings()
