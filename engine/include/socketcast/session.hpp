#pragma once
// Session state for a single stream between engine and a peer.
// TODO(Phase 1/2): sequence tracking, handshake state machine,
// per-session rate limiter + jitter buffer ownership.

#include <cstdint>
#include <string>

namespace socketcast {

enum class SessionState {
    Handshaking,
    Active,
    Closing,
    Closed,
};

class Session {
public:
    explicit Session(uint32_t stream_id) : stream_id_(stream_id) {}

    uint32_t stream_id() const { return stream_id_; }
    SessionState state() const { return state_; }

    // TODO: onPacketReceived(), onTick(), close(), etc.

private:
    uint32_t stream_id_;
    SessionState state_{SessionState::Handshaking};
};

} // namespace socketcast
