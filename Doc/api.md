# SocketCast API Reference

**Status:** Phases 1–3 implemented (C++ core). Phase 4+ (native client) in progress.  
**Note:** FastAPI control plane (Phase 5) will be documented here when complete.

---

## Table of Contents

1. [Overview](#overview)
2. [C++ Engine API](#c-engine-api)
3. [Native Client API](#native-client-api)
4. [CLI Tools](#cli-tools)
5. [FastAPI Control Plane](#fastapi-control-plane-phase-5)

---

## Overview

SocketCast has three API surfaces:

1. **C++ Core** (Phases 1–3) — Transport, session, rate control, media
2. **Native Client** (Phase 4) — Playback via SDL2 or headless
3. **FastAPI** (Phase 5) — Session management, WebSocket bridge, metrics

This document covers C++ and native client. FastAPI will be added in Phase 5.

---

## C++ Engine API

### Core Components

All classes are in namespace `socketcast`.

#### Transport

```cpp
#include "socketcast/transport.hpp"

class Transport {
public:
  // Constructor: bind to local address and port
  Transport(std::string bind_address, uint16_t port);
  ~Transport();

  // Configure for dummy send (Phase 1)
  void configure_send(const std::string& peer_host, uint16_t peer_port,
                      uint32_t packet_count, uint16_t payload_size);

  // Configure for media streaming (Phase 3)
  void configure_stream(const std::string& peer_host, uint16_t peer_port,
                        const std::string& input_path, uint32_t fps = 30);

  // Configure for receiving (Phase 3)
  void configure_receive(const std::string& output_path);

  // Set callback for received packets
  using RxCallback = std::function<void(const Packet&, const Session::Stats&)>;
  void set_rx_callback(RxCallback cb);

  // Run the event loop (blocking until user stops)
  void run();

  // Get current session statistics
  const Session::Stats* stats() const;

  // Get bound port
  uint16_t bound_port() const;
};
```

**Example: Listen and receive**

```cpp
socketcast::Transport transport("0.0.0.0", 5000);
transport.configure_receive("output.h264");
transport.set_rx_callback([](const Packet& pkt, const Session::Stats& stats) {
  std::cout << "Received " << pkt.payload.size() << " bytes\n";
});
transport.run();  // Blocks; Ctrl+C to stop
```

---

#### Packet

```cpp
#include "socketcast/packet.hpp"

struct PacketHeader {
  uint32_t magic{0x53435354};      // "SCST"
  uint8_t version{1};
  PacketType packet_type{PacketType::Data};
  FrameType frame_type{FrameType::Control};
  uint8_t flags{0};                // SYN=0x01, ACK=0x02, FIN=0x04
  uint32_t stream_id{0};
  uint32_t sequence_number{0};
  uint64_t timestamp_us{0};
  uint16_t payload_length{0};
  uint16_t reserved{0};
  uint32_t checksum{0};
};

class Packet {
public:
  // Parse incoming datagram
  static std::optional<Packet> deserialize(const uint8_t* data, size_t len);

  // Encode to wire format
  size_t serialize(uint8_t* out, size_t out_capacity) const;

  PacketHeader header{};
  std::vector<uint8_t> payload;
};

// Compute CRC-32 ISO 3309
uint32_t crc32(const uint8_t* data, size_t len);
```

**Example: Build and send a packet**

```cpp
socketcast::Packet pkt;
pkt.header.packet_type = socketcast::PacketType::Data;
pkt.header.frame_type = socketcast::FrameType::Keyframe;
pkt.header.stream_id = 1;
pkt.header.sequence_number = 42;
pkt.payload = {0xAA, 0xBB, 0xCC, ...};

uint8_t buffer[1232];  // header(32) + payload(1200)
size_t bytes = pkt.serialize(buffer, sizeof(buffer));

sendto(sock, buffer, bytes, 0, &peer_addr, sizeof(peer_addr));
```

---

#### Session

```cpp
#include "socketcast/session.hpp"

class Session {
public:
  enum class Role { Listener, Initiator };

  struct Stats {
    uint64_t data_sent{0};
    uint64_t data_received{0};
    uint64_t acked{0};
    uint64_t retransmits{0};
    uint64_t nacks_sent{0};
    uint64_t deadline_drops{0};       // Phase 3+
    uint64_t media_bytes_received{0}; // Phase 3+
  };

  // Constructor
  Session(Role role, sockaddr_in peer);

  // Accessors
  uint32_t stream_id() const;
  SessionState state() const;  // Handshaking, Active, Closing, Closed
  const Stats& stats() const;
  bool failed() const;
  bool is_complete() const;

  // Phase 1: Dummy send
  void set_dummy_send(uint32_t count, uint16_t payload_size);

  // Phase 3: Media streaming
  void set_media_send(std::unique_ptr<MediaSource> source);
  void set_media_receive(std::unique_ptr<MediaSink> sink);

  // Start handshake (initiator only)
  std::vector<Packet> start();

  // Process incoming packet, return packets to send
  std::vector<Packet> on_packet(const Packet& pkt,
                               std::chrono::steady_clock::time_point now);

  // Periodic tick (RTO check, rate control, etc.)
  std::vector<Packet> on_tick(std::chrono::steady_clock::time_point now);

  // Graceful close
  void begin_close();
};
```

**Example: Simple send session**

```cpp
socketcast::Session session(socketcast::Session::Role::Initiator, peer_addr);
session.set_dummy_send(100, 64);  // 100 packets, 64 bytes each

// Handshake
auto pkts = session.start();
for (const auto& pkt : pkts) {
  sendto(sock, ...);
}

// Event loop
while (!session.is_complete()) {
  // Receive
  uint8_t buffer[1232];
  recvfrom(sock, buffer, sizeof(buffer), ...);
  auto pkt = Packet::deserialize(buffer, size);
  
  auto response = session.on_packet(*pkt, now);
  for (const auto& resp : response) {
    sendto(sock, ...);
  }
  
  // Timer tick
  auto ticks = session.on_tick(now);
  for (const auto& tick : ticks) {
    sendto(sock, ...);
  }
}

std::cout << "Sent: " << session.stats().data_sent << "\n";
```

---

#### Rate Controller

```cpp
#include "socketcast/rate_controller.hpp"

class RateController {
public:
  enum class BitrateDropReason {
    NoSignal,      // Stable
    HighLoss,      // > 5% loss
    RttIncreasing, // RTT trending up
  };

  struct BitrateSignal {
    uint32_t rate_bps;
    BitrateDropReason reason;
  };

  explicit RateController(uint32_t initial_rate_bps);

  // Call on timer tick (every 1ms or similar)
  void onTick(std::chrono::steady_clock::time_point now);

  // Call when DATA ACK received
  void onAck(std::chrono::microseconds rtt);

  // Call when packet loss detected (NACK or timeout)
  void onLoss();

  // Query current rate
  BitrateSignal currentSignal() const;

  // Token bucket: bytes available to send
  uint32_t availableTokens() const;

  // Consume tokens (returns actual consumed)
  uint32_t consumeTokens(uint32_t bytes);
};
```

**Example: Rate-limited sending**

```cpp
socketcast::RateController rc(1'000'000);  // 1 Mbps

while (streaming) {
  rc.onTick(now);
  
  // Check how much we can send
  uint32_t can_send = rc.availableTokens();
  
  if (can_send >= packet_size) {
    sendto(sock, packet, packet_size, ...);
    rc.consumeTokens(packet_size);
  } else {
    // Wait for next tick
  }
  
  // On ACK received
  rc.onAck(rtt_sample);
  
  // On NACK received
  rc.onLoss();
  
  auto signal = rc.currentSignal();
  std::cout << "Rate: " << signal.rate_bps << " bps, reason: "
            << (int)signal.reason << "\n";
}
```

---

#### Jitter Buffer

```cpp
#include "socketcast/jitter_buffer.hpp"

class JitterBuffer {
public:
  struct BufferedPacket {
    uint32_t sequence_number;
    uint64_t arrival_time_us;
    uint16_t payload_size;
    uint8_t priority;  // 0=low (P-frame), 1=high (I-frame)
  };

  explicit JitterBuffer(uint32_t target_depth_ms);

  // Push received packet (returns false if rejected)
  bool push(uint32_t seq, uint64_t arrival_time_us,
            uint16_t payload_size, uint8_t priority);

  // Pop when ready for playback (returns empty if underrun)
  std::optional<BufferedPacket> pop(uint64_t now_us);

  // Query buffer state
  uint32_t current_depth_ms() const;
  uint32_t jitter_estimate_ms() const;
  uint32_t fullness_percent() const;
};
```

**Example: Buffering and playback**

```cpp
socketcast::JitterBuffer jb(50);  // 50ms target depth

// On packet arrival
jb.push(seq, arrival_us, payload_size, is_keyframe ? 1 : 0);

// On playback tick
auto pkt = jb.pop(now_us);
if (pkt) {
  render(pkt->data);
} else {
  std::cout << "Underrun (freeze)\n";
}

std::cout << "Buffer depth: " << jb.current_depth_ms() << "ms\n"
          << "Jitter: " << jb.jitter_estimate_ms() << "ms\n"
          << "Fullness: " << jb.fullness_percent() << "%\n";
```

---

#### Media Source & Sink

```cpp
#include "socketcast/media_source.hpp"
#include "socketcast/media_sink.hpp"

struct MediaChunk {
  std::vector<uint8_t> payload;
  FrameType frame_type;
  uint64_t pts_us;
};

class MediaSource {
public:
  MediaSource(std::string path, uint32_t fps = 30);
  bool open();
  bool eof() const;
  std::vector<MediaChunk> next_chunks(size_t max_chunks = 16);
};

class MediaSink {
public:
  MediaSink(std::string path);
  bool open();
  void write(const std::vector<uint8_t>& data);
  uint64_t bytes_written() const;
};
```

---

## Native Client API

### PlaybackClient

```cpp
#include "socketcast_client/client.hpp"

class PlaybackClient {
public:
  explicit PlaybackClient(const std::string& server_addr, uint16_t server_port);
  ~PlaybackClient();

  // Connect to server
  bool connect();

  // Disconnect from server
  bool disconnect();

  // Check connection state
  bool is_connected() const;

  // Run event loop (blocking, ~30 FPS)
  int run();

  // Get playback statistics
  struct PlaybackStats {
    uint64_t frames_received{0};
    uint64_t packets_received{0};
    uint64_t bytes_received{0};
    uint32_t current_bitrate_kbps{0};
  };
  const PlaybackStats& stats() const;
};
```

**Example: Playback client**

```cpp
socketcast::PlaybackClient client("192.168.1.100", 5000);

if (!client.connect()) {
  std::cerr << "Failed to connect\n";
  return 1;
}

int ret = client.run();  // Blocks until Ctrl+C or error

const auto& stats = client.stats();
std::cout << "Received: " << stats.packets_received << " packets, "
          << stats.bytes_received << " bytes, "
          << stats.current_bitrate_kbps << " kbps\n";

return ret;
```

---

## CLI Tools

### Engine

```bash
# Listen (receiver mode)
./socketcast_engine listen [--bind ADDRESS] [--port PORT] [--output FILE.h264]

# Send (dummy sender, Phase 1)
./socketcast_engine send --host HOST --port PORT [--count N] [--size BYTES]

# Stream (media sender, Phase 3+)
./socketcast_engine stream --host HOST --port PORT --input FILE [--fps N]
```

**Examples:**

```bash
# Receive stream and write to file
./socketcast_engine listen --bind 0.0.0.0 --port 5000 --output received.h264

# Send 100 dummy packets
./socketcast_engine send --host 127.0.0.1 --port 5000 --count 100 --size 64

# Stream real video (50 fps)
./socketcast_engine stream --host 127.0.0.1 --port 5000 --input video.h264 --fps 50
```

### Native Client

```bash
./socketcast_client SERVER [PORT]
```

**Examples:**

```bash
# Connect to local server
./socketcast_client 127.0.0.1 5000

# Connect to remote server
./socketcast_client streaming.example.com 5000
```

---

## FastAPI Control Plane (Phase 5)

**Status:** Not yet implemented. Placeholder for future API.

Phase 5 will add Python FastAPI endpoints for:

- **Session management** — create/list/delete sessions
- **Metrics** — query bitrate, loss, RTT, jitter for a session
- **WebSocket bridge** — proxy video frames to web browsers
- **Rate limiting** — Redis-backed token bucket for per-session rate limits

Endpoints TBD.

---

## Version History

| Date | Version | Component | Status |
|------|---------|-----------|--------|
| 2026-08-24 | 1.0 | Transport, Packet, Session, RTO | Complete |
| 2026-08-25 | 1.0 | RateController, JitterBuffer | Complete |
| 2026-08-27 | 1.0 | MediaSource, MediaSink, NAL parsing | Complete |
| 2026-08-29 | 1.0 | PlaybackClient (native) | MVP complete |
| TBD | 2.0 | FastAPI, WebSocket, Redis | Not started |
