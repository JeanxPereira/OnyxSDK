#pragma once

#include <Onyx/Core/Result.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace Onyx::Media {

// One started miniaudio playback device producing interleaved float32 at a fixed rate and channel
// count. `fill` runs on the device's thread and must write `frames * channels` samples.
class PlaybackDevice {
public:
    using Fill = std::function<void(float* out, uint32_t frames)>;

    PlaybackDevice();
    ~PlaybackDevice();

    PlaybackDevice(const PlaybackDevice&) = delete;
    PlaybackDevice& operator=(const PlaybackDevice&) = delete;

    // Closes the open device first. media.audio when no device can be opened or started.
    Result<void> Open(uint32_t rate, uint32_t channels, Fill fill);
    // Returns once the callback can no longer run.
    void Close();
    // Stop returns once the callback can no longer run; Start resumes it. Both are no-ops without
    // an open device.
    Result<void> Start();
    void Stop();

    bool IsOpen() const;
    bool Running() const;
    // Seconds of audio the device buffers ahead of what it has played; 0 without an open device.
    double Latency() const;
    uint32_t Rate() const { return m_rate; }
    uint32_t Channels() const { return m_channels; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    uint32_t m_rate = 0;
    uint32_t m_channels = 0;
};

}
