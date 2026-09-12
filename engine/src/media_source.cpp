#include "socketcast/media_source.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <thread>

namespace socketcast {
namespace {

bool ends_with(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool is_raw_h264(const std::string& path) {
    return ends_with(path, ".h264") || ends_with(path, ".264") || ends_with(path, ".bin");
}

// ISO 13818-7 Table 35: ADTS sampling_frequency_index -> Hz.
constexpr uint32_t kAdtsSampleRates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                         22050, 16000, 12000, 11025, 8000,  7350};

// AAC frames are always 1024 PCM samples; duration follows from sample rate alone.
uint64_t adts_frame_duration_us(uint32_t sample_rate) {
    if (sample_rate == 0) {
        return 21'333;  // 1024/48000
    }
    return (1024ULL * 1'000'000ULL) / sample_rate;
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
    const bool raw_video = is_raw_h264(path_);
    if (raw_video) {
        // Raw .h264/.264/.bin sources carry no container, so no audio track to pull.
        opened_ = load_annex_b_file();
    } else {
        // Run video/audio ffmpeg transcodes concurrently — each is a
        // multi-second blocking popen(); serial would double the wait.
        std::thread audio_thread([this] { load_audio_via_ffmpeg(); });
        opened_ = load_via_ffmpeg();
        audio_thread.join();
    }
    if (opened_) {
        build_chunk_queue();
        if (!raw_video) {
            build_audio_chunk_queue();
        }
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
    // No -tune zerolatency: it enables x264 sliced-threads, splitting each
    // picture into multiple VCL NALs — but this class assumes one NAL is one
    // picture (pts, per-NAL flushing), so slicing corrupted pts and images.
    const std::string cmd =
        "ffmpeg -loglevel error -y -i \"" + path_ +
        "\" -an -c:v libx264 -preset ultrafast -f h264 pipe:1";
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

void MediaSource::load_audio_via_ffmpeg() {
    // Best-effort: many inputs have no audio track. adts = self-framed AAC, no container needed.
    const std::string cmd = "ffmpeg -loglevel error -y -i \"" + path_ +
                            "\" -vn -c:a aac -f adts pipe:1 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
        return;
    }
    std::array<uint8_t, 8192> buf{};
    size_t n = 0;
    while ((n = fread(buf.data(), 1, buf.size(), pipe)) > 0) {
        adts_.insert(adts_.end(), buf.data(), buf.data() + n);
    }
    pclose(pipe);
    // No audio track (or extraction failed) -> adts_ stays empty, has_audio() is false.
}

void MediaSource::build_audio_chunk_queue() {
    audio_pending_.clear();
    audio_cursor_ = 0;
    if (adts_.size() < 7) {
        return;
    }

    uint64_t pts_us = 0;
    size_t i = 0;
    while (i + 7 <= adts_.size()) {
        if (adts_[i] != 0xFF || (adts_[i + 1] & 0xF0) != 0xF0) {
            ++i;
            continue;
        }
        const bool protection_absent = adts_[i + 1] & 0x01;
        const size_t header_len = protection_absent ? 7 : 9;
        const uint32_t sr_index = (adts_[i + 2] >> 2) & 0x0F;
        const uint8_t channels =
            static_cast<uint8_t>(((adts_[i + 2] & 0x01) << 2) | ((adts_[i + 3] >> 6) & 0x03));
        const uint32_t frame_length = ((adts_[i + 3] & 0x03u) << 11) |
                                      (static_cast<uint32_t>(adts_[i + 4]) << 3) |
                                      ((adts_[i + 5] >> 5) & 0x07u);

        if (sr_index >= sizeof(kAdtsSampleRates) / sizeof(kAdtsSampleRates[0]) ||
            frame_length < header_len || i + frame_length > adts_.size()) {
            ++i;
            continue;
        }

        const uint32_t sample_rate = kAdtsSampleRates[sr_index];
        AudioChunk chunk;
        chunk.payload.assign(adts_.begin() + static_cast<long>(i),
                             adts_.begin() + static_cast<long>(i + frame_length));
        chunk.pts_us = pts_us;
        chunk.sample_rate = sample_rate;
        chunk.channels = channels > 0 ? channels : 2;
        audio_pending_.push_back(std::move(chunk));

        pts_us += adts_frame_duration_us(sample_rate);
        i += frame_length;
    }
}

std::vector<AudioChunk> MediaSource::next_audio_chunks(size_t max_chunks) {
    std::vector<AudioChunk> out;
    while (out.size() < max_chunks && audio_cursor_ < audio_pending_.size()) {
        out.push_back(audio_pending_[audio_cursor_++]);
    }
    return out;
}

}  // namespace socketcast
