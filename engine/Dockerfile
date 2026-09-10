# syntax=docker/dockerfile:1

# ---- builder ----
FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN rm -rf build \
    && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --target socketcast_engine

# ---- runtime ----
FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 curl ffmpeg \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/socketcast_engine /usr/local/bin/socketcast_engine

# Media transport — UDP data port + (future) Prometheus metrics port.
EXPOSE 5000/udp
EXPOSE 9100/tcp

ENTRYPOINT ["/usr/local/bin/socketcast_engine"]
CMD ["listen", "--bind", "0.0.0.0", "--port", "5000"]
