#include "socketcast/audio_buffer.hpp"

namespace socketcast {

AudioBuffer::AudioBuffer(size_t max_frames) : max_frames_(max_frames) {}

bool AudioBuffer::push_frame(const std::vector<uint8_t>& data, uint64_t timestamp_us,
                             uint32_t sample_rate, uint8_t channels) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Drop oldest frame if buffer is full
    if (buffer_.size() >= max_frames_) {
        buffer_.erase(buffer_.begin());
    }

    auto frame = std::make_shared<AudioFrame>();
    frame->data = data;
    frame->timestamp_us = timestamp_us;
    frame->sample_rate = sample_rate;
    frame->channels = channels;

    buffer_.push_back(frame);
    ++total_frames_;
    return true;
}

std::vector<std::shared_ptr<AudioFrame>> AudioBuffer::get_all_frames() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::shared_ptr<AudioFrame>> result(buffer_.begin(), buffer_.end());
    buffer_.clear();
    return result;
}

size_t AudioBuffer::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_.size();
}

size_t AudioBuffer::total_frames_added() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_frames_;
}

}  // namespace socketcast
