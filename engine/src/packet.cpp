#include "socketcast/packet.hpp"

namespace socketcast {

Packet Packet::deserialize(const uint8_t* /*data*/, size_t /*len*/) {
    // TODO(Phase 1): parse header + payload from raw bytes, validate checksum.
    return Packet{};
}

size_t Packet::serialize(uint8_t* /*out*/, size_t /*out_capacity*/) const {
    // TODO(Phase 1): write header + payload into out buffer.
    return 0;
}

} // namespace socketcast
