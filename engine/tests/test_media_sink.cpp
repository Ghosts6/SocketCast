#include "socketcast/media_sink.hpp"
#include "test_util.hpp"

#include <fstream>
#include <iostream>
#include <vector>

int main() {
    const char* path = "/tmp/socketcast_sink_test.h264";
    socketcast::MediaSink sink(path);
    require(sink.open(), "open sink");

    const std::vector<uint8_t> annex_b = {0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1E};
    sink.write(annex_b);
    sink.write({});  // no-op

    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> out((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    require(out == annex_b, "annex b roundtrip");

    std::remove(path);
    std::cout << "test_media_sink ok\n";
    return 0;
}
