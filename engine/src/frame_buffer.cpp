#include "socketcast/frame_buffer.hpp"

namespace socketcast {

FrameBuffer::FrameBuffer(size_t max_frames) : max_frames_(max_frames) {}

bool FrameBuffer::push_frame(const std::vector<uint8_t>& data, uint64_t timestamp_us,
                             bool is_keyframe) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Drop oldest frame if buffer is full
    if (buffer_.size() >= max_frames_) {
        buffer_.erase(buffer_.begin());
    }

    auto frame = std::make_shared<Frame>();
    frame->data = data;
    frame->timestamp_us = timestamp_us;
    frame->is_keyframe = is_keyframe;

    buffer_.push_back(frame);
    ++total_frames_;
    return true;
}

std::shared_ptr<Frame> FrameBuffer::peek_latest() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (buffer_.empty()) {
        return nullptr;
    }
    return buffer_.back();
}

std::vector<std::shared_ptr<Frame>> FrameBuffer::get_all_frames() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::shared_ptr<Frame>> result(buffer_.begin(), buffer_.end());
    buffer_.clear();
    return result;
}

size_t FrameBuffer::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_.size();
}

size_t FrameBuffer::total_frames_added() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_frames_;
}

}  // namespace socketcast
