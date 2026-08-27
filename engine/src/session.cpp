#include "socketcast/session.hpp"

#include <algorithm>
#include <cstring>

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

}  // namespace

Session::Session(Role role, sockaddr_in peer) : role_(role), peer_(peer) {
    if (role_ == Role::Listener) {
        stream_id_ = kListenerStreamId;
    }
}

void Session::set_dummy_send(uint32_t count, uint16_t payload_size) {
    dummy_remaining_ = count;
    payload_size_ = std::max<uint16_t>(payload_size, 4);
    if (payload_size_ > kMaxPayloadLength) {
        payload_size_ = kMaxPayloadLength;
    }
}

bool Session::is_complete() const {
    if (failed_ || role_ != Role::Initiator) {
        return false;
    }
    return state_ != SessionState::Handshaking && dummy_remaining_ == 0 &&
           in_flight_.empty();
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
    pkt.header.timestamp_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());
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

std::vector<Packet> Session::fill_window(std::chrono::steady_clock::time_point now) {
    std::vector<Packet> out;
    if (role_ != Role::Initiator || state_ != SessionState::Active) {
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

std::vector<Packet> Session::deliver_in_order() {
    std::vector<Packet> acks;
    while (true) {
        auto it = reorder_.find(next_expected_);
        if (it == reorder_.end()) {
            break;
        }
        ++stats_.data_received;
        acks.push_back(make_ack(next_expected_));
        reorder_.erase(it);
        ++next_expected_;
    }
    return acks;
}

std::vector<Packet> Session::on_packet(const Packet& pkt,
                                       std::chrono::steady_clock::time_point now) {
    std::vector<Packet> out;
    const auto type = pkt.header.packet_type;

    if (type == PacketType::Handshake) {
        const uint8_t f = pkt.header.flags;
        if (role_ == Role::Listener && (f & kFlagSyn) && !(f & kFlagAck) && !(f & kFlagFin)) {
            stream_id_ = kListenerStreamId;
            last_handshake_sent_ = now;
            handshake_retries_ = 0;
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
            fin_received_ = true;
            out.push_back(make_handshake(kFlagFin | kFlagAck));
            if (f & kFlagAck) {
                state_ = SessionState::Closed;
            } else {
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
            auto acks = deliver_in_order();
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
        if (role_ == Role::Initiator && dummy_remaining_ == 0 && in_flight_.empty() &&
            !fin_sent_) {
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
            rate_controller_.onLoss();
            it->second.last_sent = now;
            ++it->second.retries;
            ++stats_.retransmits;
            out.push_back(it->second.packet);
        }
        return out;
    }

    return out;
}

std::vector<Packet> Session::on_tick(std::chrono::steady_clock::time_point now) {
    std::vector<Packet> out;
    rate_controller_.onTick(now);
    const auto rto = rto_.currentRto();

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
        for (auto& entry : in_flight_) {
            auto& slot = entry.second;
            if (now - slot.last_sent >= rto) {
                ++slot.retries;
                if (slot.retries > kMaxRetries) {
                    rate_controller_.onLoss();
                    failed_ = true;
                    state_ = SessionState::Closed;
                    return {};
                }
                rate_controller_.onLoss();
                slot.last_sent = now;
                ++stats_.retransmits;
                out.push_back(slot.packet);
            }
        }
        auto more = fill_window(now);
        out.insert(out.end(), more.begin(), more.end());
    }
    return out;
}

}  // namespace socketcast
