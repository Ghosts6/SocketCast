# SocketCast Protocol (v1) - Detailed Specification

**Version:** 1 (Frozen)  
**Status:** Phases 0–2 complete; Phases 3–4 in progress  
**Date:** August 2026

---

## Table of Contents

1. [Design Philosophy](#design-philosophy)
2. [Packet Format](#packet-format)
3. [Handshake & State Machine](#handshake--state-machine)
4. [Error Recovery](#error-recovery)
5. [Rate Control & Congestion](#rate-control--congestion)
6. [Jitter Buffer & Playback](#jitter-buffer--playback)
7. [Deadline-Based Drop Logic](#deadline-based-drop-logic)
8. [Examples](#examples)

---

## Design Philosophy

**Core principle:** Media is time-sensitive. Late data is often worse than lost data.

SocketCast is **not** TCP-like. It doesn't try to recover every packet. Instead:

1. **Every packet carries its deadline** — when it's needed for playback
2. **Sender knows the cost** of loss vs. waiting for recovery
3. **Receiver drops packets that arrive too late** — no point retransmitting them
4. **Congestion is detected early** — RTT rise signals a building queue before loss hits

This makes SocketCast suitable for **low-latency live streaming** under **lossy networks**.

---

## Packet Format

Every SocketCast datagram is exactly one packet: **32-byte header + payload**.

### Header Layout (32 bytes, big-endian)

```
Bytes  0–3   magic           0x53435354 ("SCST")
Byte   4     version         1
Byte   5     packet_type     Data=0, Ack=1, Nack=2, ReceiverReport=3, Keepalive=4, Handshake=5
Byte   6     frame_type      Keyframe=0, PFrame=1, Audio=2, Control=3
Byte   7     flags           SYN=0x01, ACK=0x02, FIN=0x04
Bytes  8–11  stream_id       Session ID (0 on initial SYN; assigned by listener)
Bytes  12–15 sequence_number Packet seq (DATA packets), or DATA seq being ACK'd/NACK'd
Bytes  16–23 timestamp_us    Sender's clock (us since epoch, for jitter/playback, not RTT)
Bytes  24–25 payload_length  Bytes in payload (max 1200)
Bytes  26–27 reserved        Must be 0 (reject non-zero in v1)
Bytes  28–31 checksum        CRC-32 ISO 3309 (whole packet with checksum field = 0)
Bytes  32+   payload         Variable length (up to 1200 bytes)
```

### Packet Types

| Type | Name | Phase | Purpose |
|------|------|-------|---------|
| 0 | DATA | 1 | Media payload |
| 1 | ACK | 1 | Positive acknowledgment (packet received) |
| 2 | NACK | 1 | Negative acknowledgment (retransmit request) |
| 3 | RECEIVER_REPORT | 2+ | Statistics report (future) |
| 4 | KEEPALIVE | 2+ | Keep-alive probe (future) |
| 5 | HANDSHAKE | 1 | Session setup/teardown (SYN/ACK/FIN) |

### Frame Types (Priority Hierarchy)

| Type | Priority | Phase | Description |
|------|----------|-------|-------------|
| 0 | High | 3 | Keyframe (I-frame, full frame data) |
| 1 | Medium | 3 | P-frame (predicted frame, delta only) |
| 2 | High | 3 | Audio (same priority as keyframe) |
| 3 | None | 1 | Control (handshake, dummy, no media) |

**Phase 1–2 use CONTROL only.** Phase 3+ classifies media via NAL parsing.

### Flags (Handshake Only)

Used **only on HANDSHAKE packets**. All other packet types have `flags = 0`.

| Bit | Name | Meaning |
|-----|------|---------|
| 0x01 | SYN | Initiate handshake |
| 0x02 | ACK | Acknowledge (handshake response) |
| 0x04 | FIN | Close session |

Examples:
- `SYN`: Initiator → Listener, open session
- `SYN \| ACK`: Listener → Initiator, accept and assign stream_id
- `ACK`: Initiator → Listener, confirm
- `FIN \| ACK`: Either party, graceful close

---

## Handshake & State Machine

### Three-Way Handshake

```
Initiator                    Listener
   |                           |
   |--- HANDSHAKE(SYN) ------->| stream_id not yet known
   |   stream_id=0, seq=0      |
   |                           | Listener assigns stream_id = 1
   |<----- HANDSHAKE(SYN|ACK) -| stream_id=1, seq=0
   |      stream_id=1          |
   |                           |
   |--- HANDSHAKE(ACK) ------->| Confirmed
   |   stream_id=1, seq=0      |
   |                           |
   |<------- DATA(seq=1) ------| Active streaming begins
   |                           |
```

### Session States

```
Initiator:
  IDLE → (call start()) → HANDSHAKING
  HANDSHAKING → (receive SYN|ACK) → ACTIVE
  ACTIVE → (send FIN or all packets acked) → CLOSING
  CLOSING → (receive FIN|ACK) → CLOSED

Listener:
  IDLE → (receive SYN) → HANDSHAKING
  HANDSHAKING → (send SYN|ACK) → WAITING_FOR_ACK
  WAITING_FOR_ACK → (receive ACK) → ACTIVE
  ACTIVE → (receive FIN) → CLOSING
  CLOSING → (send FIN|ACK) → CLOSED
```

### Sequence Numbers

- **Handshake packets:** `sequence_number = 0` (do not consume sequence space)
- **DATA packets:** `sequence_number` starts at 0 after ACTIVE; increments per packet
- **ACK/NACK packets:** `sequence_number` indicates which DATA packet is being acknowledged

---

## Error Recovery

### Selective-Repeat ARQ (Automatic Repeat reQuest)

**Not stop-and-wait.** Multiple packets can be in flight.

#### Send Side

```
Send Window (size 8):
  [0] [1] [2] [3] [4] [5] [6] [7] [next_seq=8]
   ✓   ✓   ✗   ✓   ✗   ✓   ✗   ✗    <- ✓=acked, ✗=awaiting ACK

When ACK(2) arrives: drop [2] from in_flight
When NACK(2) arrives: retransmit [2] only, don't touch others
When RTO fires for [3]: retransmit [3] only
```

#### Receive Side

```
Expected in order: 0, 1, 2, 3, 4, ...
Incoming: 2, 4, 1, 5, ...

Reorder buffer:
  [1: DATA] [2: DATA] [4: DATA] [5: DATA]

When DATA(3) arrives:
  [1: DATA] [2: DATA] [3: DATA] [4: DATA] [5: DATA]
  → Deliver 1, 2, 3, 4, 5 in order to application
```

### Acknowledgments

**ACK packet:** Positive confirmation

```
  Sender → Receiver: DATA(seq=42)
  Receiver → Sender: ACK(sequence_number=42, stream_id=...)
  // Packet 42 is now confirmed acked
```

**NACK packet:** Request retransmit

```
  Sender → Receiver: DATA(seq=0), DATA(seq=1), DATA(seq=3)
  // seq 2 missing
  Receiver → Sender: NACK(sequence_number=2)
  // Sender retransmits seq 2 only
```

### Retransmit Timeout (RTO)

Uses **Jacobson's algorithm** (RFC 6298):

```
On ACK receipt:
  RTT_sample = now - send_time
  SRTT += 0.125 * (RTT_sample - SRTT)
  RTTVAR += 0.25 * (|RTT_sample - SRTT| - RTTVAR)
  RTO = SRTT + 4 * RTTVAR
  RTO = clamp(RTO, 50ms, 2000ms)

On timeout:
  if (now - last_sent >= RTO):
    retransmit packet
    retries++
    if (retries > 20):
      session FAILED
```

---

## Rate Control & Congestion

### Token-Bucket Rate Limiter

Smooths transmission at a target bitrate. Prevents burst flooding.

```
token_rate = rate_bps / 8 bytes/second
max_tokens = 1 second worth

On tick (e.g., 1ms):
  elapsed = now - last_refill
  tokens += (token_rate / 1000) * elapsed
  tokens = clamp(tokens, 0, max_tokens)
  last_refill = now

When sending DATA(N bytes):
  if (tokens >= N):
    send()
    tokens -= N
  else:
    defer (try again next tick)
```

### BBR-Inspired Congestion Backoff

Detects congestion before severe loss:

```
Track:
  - RTT samples (sliding window, last 32)
  - Loss ratio (packets_lost / packets_sent)
  - RTT baseline (initial RTT after handshake)

Each ACK/Loss event:
  recent_rtt = mean(last 4 RTT samples)
  old_rtt = mean(first 4 RTT samples)
  loss_ratio = packets_lost / packets_sent

Decision:
  if (loss_ratio > 5%):
    rate_bps *= 0.8  // Drop by 20%
    reason = HIGH_LOSS
  else if (recent_rtt > old_rtt + 10ms):
    rate_bps *= 0.9  // Drop by 10%
    reason = RTT_INCREASING  (congestion signal)
  else if (loss_ratio < 1%):
    rate_bps *= 1.05  // Slow-start increase by 5%
    reason = STABLE

  rate_bps = clamp(rate_bps, 100kbps, 100Mbps)
```

**Rationale:** Rising RTT signals a building queue (congestion) before packet loss manifests. This detects and backs off early.

---

## Jitter Buffer & Playback

### Adaptive Jitter Buffer

Buffers arrived packets to smooth out arrival-time variance and minimize underrun (freeze) and overrun (drop).

```
Target depth: 50ms
Buffer capacity: 256 packets

On packet arrival:
  jitter_estimate = sliding_window_stddev(inter_arrival_times)
  adaptive_depth = target_depth + jitter_estimate
  
  if (buffer_fullness > 95%):
    // Overrun: drop lowest-priority oldest packet
    drop MIN(priority, arrival_time)  // P-frames first, oldest first
  
  buffer.push(packet)

On pop (playback tick):
  if (buffer.empty()):
    // Underrun: freeze
    return EMPTY
  
  head = buffer.front()
  buffered_time = now - head.arrival_time
  
  if (buffered_time >= adaptive_depth || buffer.size() == 1):
    // Packet has aged enough, or no other packets
    deliver head
  else:
    // Still buffering
    return EMPTY
```

**Priority hierarchy:** Keyframes > P-frames (on drop)

---

## Deadline-Based Drop Logic

### Playback Deadline Formula

For each DATA packet:

```
PTS = packet.timestamp_us  (sender's clock)
max_acceptable_latency = <negotiated or config>  (typically 200ms)
safety_margin = 50ms  (buffer for retransmit/jitter)

deadline = PTS + max_acceptable_latency - safety_margin

If (now > deadline):
  drop packet
  don't retransmit
  reason: packet arrived too late, not worth recovery cost
```

### Rationale

1. **Sender knows PTS** for each media chunk
2. **Sender computes deadline** from PTS and max latency budget
3. **Sender drops late packets** in `on_tick()`
4. **Saves bandwidth:** Retransmitting a packet that can't be played is wasted bandwidth

Example:
```
MAX_LATENCY = 200ms

Sender sends:
  DATA(seq=10, timestamp_us=1000000)  deadline=1150000
  DATA(seq=11, timestamp_us=1033333)  deadline=1183333
  
If ACK(10) not received by deadline:
  if (now > 1150000):
    drop from in_flight
    don't retransmit
```

---

## Examples

### Example 1: Happy Path (Clean Network)

```
Sender                        Receiver
  |                             |
  |--- SYN (stream_id=0) ------->|
  |                              | Assign stream_id=1
  |<----- SYN|ACK (stream_id=1) -|
  |                              |
  |--- ACK ------------------>|
  |                              | ACTIVE
  |                              |
  |--- DATA(0) ------------->|
  |--- DATA(1) ------------->|
  |--- DATA(2) ------------->|
  |<-------- ACK(0) -----------|
  |<-------- ACK(1) -----------|
  |<-------- ACK(2) -----------|
  |                             | (all delivered in order)
  | ...
```

### Example 2: Packet Loss & Retransmit

```
Sender                        Receiver
  |                             |
  |--- DATA(0) ------------->|
  |--- DATA(1) ------------->| (LOST)
  |--- DATA(2) ------------->|
  |<-------- NACK(1) -----------| Need seq 1
  |                             |
  |--- DATA(1) [RETRANSMIT] --->|
  |<-------- ACK(1) -----------| Got seq 1
  |                             |
  | (Now can deliver 1, 2 in order)
```

### Example 3: RTT Rise & Congestion Backoff

```
Time  RTT        Loss Ratio  Rate Action
----  ---        ----------  -----------
t=0   50ms       0%          1000kbps
t=5s  55ms       0%          1000kbps  (stable)
t=10s 65ms       0%          1000kbps  (trending up...)
t=15s 75ms       0%          900kbps   (RTT > baseline + 10ms, BACKOFF)
t=20s 60ms       0%          945kbps   (recovering, slow-start +5%)
```

---

## References

- **RFC 6298:** Computing TCP's Retransmission Timer (Jacobson's RTO)
- **BBR Congestion Control:** (Google's BBR algorithm inspired this design)
- **H.264 Annex B:** NAL unit stream format for media frames (Phase 3+)

---

## Revision History

| Date | Version | Changes |
|------|---------|---------|
| 2026-08-24 | 1 | Protocol frozen for Phases 0–2 |
| 2026-08-27 | 1 | Phases 3–4 (media, native client) in progress |
