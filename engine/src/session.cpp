#include "socketcast/session.hpp"

#include "socketcast/nal_parser.hpp"

#include <algorithm>

namespace socketcast {
namespace {

Packet control_packet(PacketType type, uint32_t stream_id, uint32_t seq, uint8_t flags) {
    Packet pkt;
    pkt.header.packet_type = type;
    pkt.header.frame_type = FrameType::Control;
    pkt.header.flags = flags;
    pkt.header.stream_id = stream_id;
    pkt.header.sequence_number = seq;
    return pkt;
}

uint64_t steady_us(std::chrono::steady_clock::time_point tp) {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()).count());
}

}  // namespace

Session::Session(Role role, sockaddr_in peer) : role_(role), peer_(peer) {
    if (role_ == Role::Listener) {
        stream_id_ = kListenerStreamId;
    }
}

void Session::set_dummy_send(uint32_t count, uint16_t payload_size) {
    media_mode_ = false;
    dummy_remaining_ = count;
    payload_size_ = std::max<uint16_t>(payload_size, 4);
    if (payload_size_ > kMaxPayloadLength) {
        payload_size_ = kMaxPayloadLength;
    }
}

void Session::set_media_send(std::unique_ptr<MediaSource> source) {
    media_mode_ = true;
    media_source_ = std::move(source);
    dummy_remaining_ = 0;
}

void Session::set_media_receive(std::unique_ptr<MediaSink> sink) {
    media_sink_ = std::move(sink);
}

bool Session::is_complete() const {
    if (failed_ || role_ != Role::Initiator) {
        return false;
    }
    if (media_mode_) {
        return state_ != SessionState::Handshaking && media_source_ && media_source_->eof() &&
               in_flight_.empty();
    }
    return state_ != SessionState::Handshaking && dummy_remaining_ == 0 && in_flight_.empty();
}

std::vector<Packet> Session::start() {
    if (role_ != Role::Initiator) {
        return {};
    }
    last_handshake_sent_ = std::chrono::steady_clock::now();
    return {make_handshake(kFlagSyn)};
}

void Session::begin_close() {
    if (fin_sent_ || state_ == SessionState::Closed) {
        return;
    }
    state_ = SessionState::Closing;
    fin_sent_ = true;
}

Packet Session::make_handshake(uint8_t flags) const {
    return control_packet(PacketType::Handshake, stream_id_, 0, flags);
}

Packet Session::make_ack(uint32_t seq) const {
    return control_packet(PacketType::Ack, stream_id_, seq, 0);
}

Packet Session::make_nack(uint32_t seq) const {
    return control_packet(PacketType::Nack, stream_id_, seq, 0);
}

Packet Session::make_data(uint32_t seq, std::chrono::steady_clock::time_point now) const {
    Packet pkt;
    pkt.header.packet_type = PacketType::Data;
    pkt.header.frame_type = FrameType::Control;
    pkt.header.stream_id = stream_id_;
    pkt.header.sequence_number = seq;
    pkt.header.timestamp_us = steady_us(now);
    pkt.payload.resize(payload_size_);
    pkt.payload[0] = static_cast<uint8_t>(seq >> 24);
    pkt.payload[1] = static_cast<uint8_t>(seq >> 16);
    pkt.payload[2] = static_cast<uint8_t>(seq >> 8);
    pkt.payload[3] = static_cast<uint8_t>(seq);
    for (size_t i = 4; i < pkt.payload.size(); ++i) {
        pkt.payload[i] = 0xA5;
    }
    return pkt;
}

Packet Session::make_media_packet(const MediaChunk& chunk, uint32_t seq) const {
    Packet pkt;
    pkt.header.packet_type = PacketType::Data;
    pkt.header.frame_type = chunk.frame_type;
    pkt.header.stream_id = stream_id_;
    pkt.header.sequence_number = seq;
    pkt.header.timestamp_us = chunk.pts_us;
    pkt.payload = chunk.payload;
    return pkt;
}

uint8_t Session::jitter_priority(FrameType ft) const {
    return (ft == FrameType::Keyframe || ft == FrameType::Audio) ? 1 : 0;
}

bool Session::should_retransmit(const InFlight& slot,
                               std::chrono::steady_clock::time_point now) const {
    if (!media_mode_) {
        return true;
    }
    if (!stream_start_set_) {
        return true;
    }
    const uint64_t now_us = steady_us(now);
    const uint64_t stream_now = now_us - stream_start_us_;
    const uint64_t playback = slot.packet.header.timestamp_us;
    if (playback <= stream_now + kSafetyMarginUs) {
        return false;
    }
    const int64_t deadline = static_cast<int64_t>(playback - stream_now - kSafetyMarginUs);
    return static_cast<int64_t>(rto_.currentRto().count()) < deadline;
}

void Session::deliver_from_jitter(std::chrono::steady_clock::time_point now) {
    if (!media_sink_) {
        return;
    }
    const uint64_t now_us = steady_us(now);
    while (true) {
        auto pkt = jitter_buffer_.pop(now_us);
        if (!pkt) {
            break;
        }
        // Jitter buffer stores metadata only; payload is written in deliver_in_order.
        // Phase 5b: count keyframes as received frames
        (void)pkt;
    }
}

std::vector<Packet> Session::fill_window(std::chrono::steady_clock::time_point now) {
    std::vector<Packet> out;
    if (role_ != Role::Initiator || state_ != SessionState::Active) {
        return out;
    }

    rate_controller_.onTick(now);

    if (media_mode_ && media_source_) {
        while (in_flight_.size() < kSendWindow && !media_source_->eof()) {
            // Pace initial sends to the source's timeline — otherwise a
            // LAN-speed send blasts a multi-minute clip out in a few seconds.
            if (stream_start_set_) {
                const uint64_t elapsed = steady_us(now) - stream_start_us_;
                const uint64_t next_pts = media_source_->peek_next_pts_us();
                if (next_pts > elapsed + kSendAheadUs) {
                    break;
                }
            }

            const auto chunks = media_source_->next_chunks(1);
            if (chunks.empty()) {
                break;
            }
            const auto& chunk = chunks.front();
            const size_t pkt_bytes = kHeaderSize + chunk.payload.size();
            if (rate_controller_.availableTokens() < pkt_bytes) {
                break;
            }
            rate_controller_.consumeTokens(static_cast<uint32_t>(pkt_bytes));

            const uint32_t seq = next_seq_++;
            Packet pkt = make_media_packet(chunk, seq);
            if (!stream_start_set_) {
                stream_start_us_ = steady_us(now);
                stream_start_set_ = true;
            }

            // Flush on every NAL boundary so each buffered frame is exactly one
            // NAL unit (flushing only on keyframes used to glue a whole GOP's
            // NALs into one blob, corrupting the dashboard decoder's SPS parse).
            if (starts_with_annex_b(chunk.payload) && !pending_frame_.empty()) {
                if (frame_capture_cb_) {
                    frame_capture_cb_(pending_frame_, last_frame_timestamp_us_,
                                      pending_frame_is_keyframe_);
                }
                pending_frame_.clear();
            }
            if (starts_with_annex_b(chunk.payload)) {
                pending_frame_is_keyframe_ = (chunk.frame_type == FrameType::Keyframe);
                if (chunk.frame_type == FrameType::Keyframe ||
                    chunk.frame_type == FrameType::PFrame) {
                    ++stats_.frames_received;  // frames *sent* (dashboard counter)
                }
            }
            pending_frame_.insert(pending_frame_.end(), chunk.payload.begin(), chunk.payload.end());
            last_frame_timestamp_us_ = chunk.pts_us;

            InFlight slot;
            slot.packet = pkt;
            slot.last_sent = now;
            slot.retries = 0;
            in_flight_.emplace(seq, slot);
            ++stats_.data_sent;
            stats_.media_bytes_received += chunk.payload.size();  // bytes sent (reuse field for admin)
            out.push_back(std::move(pkt));
        }
        // Flush the final NAL once the source is exhausted — it would
        // otherwise never see a following start code to trigger a flush.
        if (media_source_->eof() && !pending_frame_.empty()) {
            if (frame_capture_cb_) {
                frame_capture_cb_(pending_frame_, last_frame_timestamp_us_,
                                  pending_frame_is_keyframe_);
            }
            pending_frame_.clear();
        }
        return out;
    }

    while (in_flight_.size() < kSendWindow && dummy_remaining_ > 0) {
        const uint32_t seq = next_seq_++;
        Packet pkt = make_data(seq, now);
        InFlight slot;
        slot.packet = pkt;
        slot.last_sent = now;
        slot.retries = 0;
        in_flight_.emplace(seq, slot);
        --dummy_remaining_;
        ++stats_.data_sent;
        out.push_back(std::move(pkt));
    }
    return out;
}

std::vector<Packet> Session::deliver_in_order(std::chrono::steady_clock::time_point now) {
    std::vector<Packet> acks;
    const uint64_t arrival_us = steady_us(now);
    while (true) {
        auto it = reorder_.find(next_expected_);
        if (it == reorder_.end()) {
            break;
        }
        const Packet& pkt = it->second;
        ++stats_.data_received;

        if (media_sink_) {
            media_sink_->write(pkt.payload);
            stats_.media_bytes_received += pkt.payload.size();
        }

        // Phase 5b: count keyframes as frame boundaries
        if (pkt.header.frame_type == FrameType::Keyframe) {
            ++stats_.frames_received;
        }

        jitter_buffer_.push(next_expected_, arrival_us,
                            static_cast<uint16_t>(pkt.payload.size()),
                            jitter_priority(pkt.header.frame_type));

        acks.push_back(make_ack(next_expected_));
        reorder_.erase(it);
        ++next_expected_;
    }
    deliver_from_jitter(now);
    return acks;
}

std::vector<Packet> Session::on_packet(const Packet& pkt,
                                       std::chrono::steady_clock::time_point now) {
    std::vector<Packet> out;
    const auto type = pkt.header.packet_type;

    if (type == PacketType::Handshake) {
        const uint8_t f = pkt.header.flags;
        if (role_ == Role::Listener && (f & kFlagSyn) && !(f & kFlagAck) && !(f & kFlagFin)) {
            // New initiator (including restarts): reset receive state so seq starts cleanly.
            failed_ = false;
            fin_sent_ = false;
            fin_received_ = false;
            next_expected_ = 0;
            next_seq_ = 0;
            in_flight_.clear();
            reorder_.clear();
            handshake_retries_ = 0;
            state_ = SessionState::Handshaking;
            stream_id_ = kListenerStreamId;
            last_handshake_sent_ = now;
            out.push_back(make_handshake(kFlagSyn | kFlagAck));
            return out;
        }
        if (role_ == Role::Initiator && (f & kFlagSyn) && (f & kFlagAck) && !(f & kFlagFin)) {
            stream_id_ = pkt.header.stream_id;
            out.push_back(make_handshake(kFlagAck));
            if (state_ == SessionState::Handshaking) {
                state_ = SessionState::Active;
                auto data = fill_window(now);
                out.insert(out.end(), data.begin(), data.end());
            }
            return out;
        }
        if (role_ == Role::Listener && (f & kFlagAck) && !(f & kFlagSyn) && !(f & kFlagFin) &&
            state_ == SessionState::Handshaking) {
            if (pkt.header.stream_id == stream_id_) {
                state_ = SessionState::Active;
            }
            return out;
        }
        if (f & kFlagFin) {
            // Only a bare FIN gets acked — acking a FIN|ACK would echo forever
            // over the self-loopback socket.
            if (f & kFlagAck) {
                fin_received_ = true;
                state_ = SessionState::Closed;
                return out;
            }
            if (!fin_received_) {
                fin_received_ = true;
                out.push_back(make_handshake(kFlagFin | kFlagAck));
                state_ = SessionState::Closing;
            }
            return out;
        }
        return out;
    }

    if (type == PacketType::Data) {
        if (role_ == Role::Listener && state_ == SessionState::Handshaking) {
            state_ = SessionState::Active;
        }
        if (state_ != SessionState::Active && state_ != SessionState::Closing) {
            return out;
        }
        const uint32_t seq = pkt.header.sequence_number;
        if (seq < next_expected_) {
            out.push_back(make_ack(seq));
            return out;
        }
        if (seq >= next_expected_ + kSendWindow) {
            out.push_back(make_nack(next_expected_));
            ++stats_.nacks_sent;
            return out;
        }
        reorder_.emplace(seq, pkt);
        if (seq == next_expected_) {
            auto acks = deliver_in_order(now);
            out.insert(out.end(), acks.begin(), acks.end());
        } else {
            out.push_back(make_nack(next_expected_));
            ++stats_.nacks_sent;
        }
        return out;
    }

    if (state_ != SessionState::Active && state_ != SessionState::Closing) {
        return out;
    }

    if (type == PacketType::Ack) {
        auto it = in_flight_.find(pkt.header.sequence_number);
        if (it != in_flight_.end()) {
            const auto rtt = std::chrono::duration_cast<std::chrono::microseconds>(
                now - it->second.last_sent);
            rto_.onRttSample(rtt);
            rate_controller_.onAck(rtt);
            in_flight_.erase(it);
            ++stats_.acked;
        }
        if (role_ == Role::Initiator && is_complete() && !fin_sent_) {
            fin_sent_ = true;
            state_ = SessionState::Closing;
            out.push_back(make_handshake(kFlagFin));
        } else {
            auto more = fill_window(now);
            out.insert(out.end(), more.begin(), more.end());
        }
        return out;
    }

    if (type == PacketType::Nack) {
        auto it = in_flight_.find(pkt.header.sequence_number);
        if (it != in_flight_.end()) {
            if (should_retransmit(it->second, now)) {
                rate_controller_.onLoss();
                it->second.last_sent = now;
                ++it->second.retries;
                ++stats_.retransmits;
                out.push_back(it->second.packet);
            } else {
                in_flight_.erase(it);
                ++stats_.deadline_drops;
            }
        }
        return out;
    }

    return out;
}

std::vector<Packet> Session::on_tick(std::chrono::steady_clock::time_point now) {
    std::vector<Packet> out;
    rate_controller_.onTick(now);
    const auto rto = rto_.currentRto();

    // Phase 5b: update metrics for control plane
    stats_.rtt_ms = rto_.currentRttMs();
    stats_.jitter_ms = rto_.currentJitterMs();

    if (state_ == SessionState::Handshaking) {
        if (last_handshake_sent_.time_since_epoch().count() == 0) {
            return out;
        }
        if (now - last_handshake_sent_ >= rto) {
            ++handshake_retries_;
            if (handshake_retries_ > kMaxRetries) {
                failed_ = true;
                state_ = SessionState::Closed;
                return out;
            }
            last_handshake_sent_ = now;
            if (role_ == Role::Initiator) {
                out.push_back(make_handshake(kFlagSyn));
            } else {
                out.push_back(make_handshake(kFlagSyn | kFlagAck));
            }
        }
        return out;
    }

    if (state_ == SessionState::Active || state_ == SessionState::Closing) {
        for (auto it = in_flight_.begin(); it != in_flight_.end();) {
            auto& slot = it->second;
            if (now - slot.last_sent >= rto) {
                ++slot.retries;
                if (slot.retries > kMaxRetries) {
                    rate_controller_.onLoss();
                    failed_ = true;
                    state_ = SessionState::Closed;
                    return {};
                }
                if (should_retransmit(slot, now)) {
                    rate_controller_.onLoss();
                    slot.last_sent = now;
                    ++stats_.retransmits;
                    out.push_back(slot.packet);
                } else {
                    ++stats_.deadline_drops;
                    it = in_flight_.erase(it);
                    continue;
                }
            }
            ++it;
        }
        auto more = fill_window(now);
        out.insert(out.end(), more.begin(), more.end());
        if (role_ == Role::Listener) {
            deliver_from_jitter(now);
        }
    }
    return out;
}

}  // namespace socketcast
