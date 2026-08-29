#pragma once
// Reassemble received media payloads into an Annex B file (Phase 3).

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace socketcast {

class MediaSink {
public:
    explicit MediaSink(std::string path);

    bool open();
    void write(const std::vector<uint8_t>& data);
    uint64_t bytes_written() const { return bytes_written_; }

private:
    std::string path_;
    std::ofstream out_;
    uint64_t bytes_written_{0};
};

}  // namespace socketcast
