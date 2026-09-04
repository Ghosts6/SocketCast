"""App configuration loaded from environment (.env in development)."""
from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file=".env")

    socketcast_env: str = "development"
    socketcast_log_level: str = "info"
    redis_url: str = "redis://localhost:6379/0"
    engine_host: str = "localhost"
    engine_control_port: int = 5001
    socketcast_mock_metrics: bool = False


settings = Settings()
