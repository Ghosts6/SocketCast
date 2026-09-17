// libFuzzer entrypoint for the packet parser.
// Build with SOCKETCAST_BUILD_FUZZ=ON using clang.
//
// Run:
//   ./fuzz_packet_parser -max_len=2048 corpus/

#include "socketcast/packet.hpp"
#include <cstdint>
#include <cstddef>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Feeds raw bytes straight to the parser — the goal is only that it never
    // crashes/UB's on malformed input, not that it accepts anything in particular.
    (void)socketcast::Packet::deserialize(data, size);
    return 0;
}
