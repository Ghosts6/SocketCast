"""App configuration loaded from environment (.env in development)."""
from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    socketcast_env: str = "development"
    socketcast_log_level: str = "info"
    redis_url: str = "redis://localhost:6379/0"
    engine_host: str = "localhost"
    engine_control_port: int = 5001

    class Config:
        env_file = ".env"


settings = Settings()
