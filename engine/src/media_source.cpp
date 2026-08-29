#include "socketcast/media_source.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <algorithm>

namespace socketcast {
namespace {

bool ends_with(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool is_raw_h264(const std::string& path) {
    return ends_with(path, ".h264") || ends_with(path, ".264") || ends_with(path, ".bin");
}

}  // namespace

MediaSource::MediaSource(std::string path, uint32_t fps)
    : path_(std::move(path)), fps_(fps > 0 ? fps : 30) {
    frame_duration_us_ = 1'000'000 / fps_;
}

bool MediaSource::open() {
    if (opened_) {
        return true;
    }
    opened_ = is_raw_h264(path_) ? load_annex_b_file() : load_via_ffmpeg();
    if (opened_) {
        build_chunk_queue();
    }
    return opened_;
}

bool MediaSource::load_annex_b_file() {
    std::ifstream in(path_, std::ios::binary);
    if (!in) {
        return false;
    }
    annex_b_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !annex_b_.empty();
}

bool MediaSource::load_via_ffmpeg() {
    const std::string cmd =
        "ffmpeg -loglevel error -y -i \"" + path_ +
        "\" -an -c:v libx264 -preset ultrafast -tune zerolatency -f h264 pipe:1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
        return false;
    }
    std::array<uint8_t, 8192> buf{};
    size_t n = 0;
    while ((n = fread(buf.data(), 1, buf.size(), pipe)) > 0) {
        annex_b_.insert(annex_b_.end(), buf.data(), buf.data() + n);
    }
    const int rc = pclose(pipe);
    return rc == 0 && !annex_b_.empty();
}

void MediaSource::build_chunk_queue() {
    pending_.clear();
    pending_cursor_ = 0;
    const auto units = scan_annex_b(annex_b_.data(), annex_b_.size());
    static const uint8_t kStartCode[4] = {0x00, 0x00, 0x00, 0x01};

    for (const auto& u : units) {
        uint64_t pts = 0;
        if (u.frame_type == FrameType::Keyframe || u.frame_type == FrameType::PFrame) {
            pts = frame_index_ * frame_duration_us_;
            ++frame_index_;
        }

        std::vector<uint8_t> nal_with_start;
        nal_with_start.insert(nal_with_start.end(), kStartCode, kStartCode + 4);
        nal_with_start.insert(nal_with_start.end(), annex_b_.data() + u.offset,
                              annex_b_.data() + u.offset + u.length);

        const auto pieces = chunk_payload(nal_with_start.data(), nal_with_start.size());
        for (const auto& piece : pieces) {
            pending_.push_back({piece, u.frame_type, pts});
        }
    }
    eof_ = pending_.empty();
}

std::vector<MediaChunk> MediaSource::next_chunks(size_t max_chunks) {
    std::vector<MediaChunk> out;
    if (!opened_ || eof_) {
        return out;
    }
    while (out.size() < max_chunks && pending_cursor_ < pending_.size()) {
        out.push_back(pending_[pending_cursor_++]);
    }
    if (pending_cursor_ >= pending_.size()) {
        eof_ = true;
    }
    return out;
}

}  // namespace socketcast
