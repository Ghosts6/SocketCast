#include "socketcast/media_sink.hpp"

namespace socketcast {

MediaSink::MediaSink(std::string path) : path_(std::move(path)) {}

bool MediaSink::open() {
    out_.open(path_, std::ios::binary | std::ios::trunc);
    return static_cast<bool>(out_);
}

void MediaSink::write(const std::vector<uint8_t>& data) {
    if (!out_ || data.empty()) {
        return;
    }
    out_.write(reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(data.size()));
    out_.flush();
    bytes_written_ += data.size();
}

}  // namespace socketcast
