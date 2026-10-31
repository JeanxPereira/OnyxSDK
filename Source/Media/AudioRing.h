#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace Onyx::Media {

// Interleaved float samples between the decode thread (Write) and a device callback (Read). Counts the
// frames Read handed out since the last Clear, which is the audio clock. Thread-safe.
class AudioRing {
public:
    void Reset(uint32_t channels, size_t capacityFrames);

    // Copies whole frames while there is room; returns the samples taken from `samples`.
    size_t Write(std::span<const float> samples);
    // Fills `out` with up to `frames` frames scaled by `volume` and zeroes the rest. Returns the frames read.
    size_t Read(float* out, size_t frames, float volume);
    void Clear();

    size_t BufferedFrames() const;
    size_t FreeFrames() const;
    uint64_t Consumed() const { return m_consumed.load(std::memory_order_acquire); }
    uint32_t Channels() const { return m_channels; }

private:
    mutable std::mutex m_mutex;
    std::vector<float> m_samples;
    uint32_t m_channels = 0;
    size_t m_head = 0;
    size_t m_count = 0;
    std::atomic<uint64_t> m_consumed{0};
};

}
