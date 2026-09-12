#pragma once
// Read H.264 Annex B (file or ffmpeg pipe) and produce transport chunks (Phase 3).

#include "socketcast/nal_parser.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace socketcast {

struct MediaChunk {
    std::vector<uint8_t> payload;
    FrameType frame_type{FrameType::Control};
    uint64_t pts_us{0};
};

struct AudioChunk {
    std::vector<uint8_t> payload;  // one full AAC ADTS frame (header + raw data)
    uint64_t pts_us{0};
    uint32_t sample_rate{48000};
    uint8_t channels{2};
};

class MediaSource {
public:
    explicit MediaSource(std::string path, uint32_t fps = 30);

    bool open();
    bool eof() const { return eof_; }
  std::vector<MediaChunk> next_chunks(size_t max_chunks = 16);

    bool has_audio() const { return !audio_pending_.empty(); }
    std::vector<AudioChunk> next_audio_chunks(size_t max_chunks = 16);

private:
    bool load_via_ffmpeg();
    bool load_annex_b_file();
    void build_chunk_queue();
    void load_audio_via_ffmpeg();
    void build_audio_chunk_queue();

    std::string path_;
    uint32_t fps_;
    uint64_t frame_index_{0};
    uint64_t frame_duration_us_{33'333};
    bool eof_{false};
    bool opened_{false};
    std::vector<uint8_t> annex_b_;
    size_t nal_cursor_{0};
    std::vector<MediaChunk> pending_;
    size_t pending_cursor_{0};

    std::vector<uint8_t> adts_;
    std::vector<AudioChunk> audio_pending_;
    size_t audio_cursor_{0};
};

}  // namespace socketcast
