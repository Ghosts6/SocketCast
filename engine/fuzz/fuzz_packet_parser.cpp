// libFuzzer entrypoint for the packet parser — see Doc/dev/03-roadmap-and-scope.md,
// Phase 8. Build with SOCKETCAST_BUILD_FUZZ=ON using clang.
//
// Run:
//   ./fuzz_packet_parser -max_len=2048 corpus/

#include "socketcast/packet.hpp"
#include <cstdint>
#include <cstddef>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // TODO: once Packet::deserialize is implemented, feed it raw bytes here
    // and make sure it never crashes/UB's on malformed input.
    (void)socketcast::Packet::deserialize(data, size);
    return 0;
}
