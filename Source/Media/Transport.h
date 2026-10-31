#pragma once

#include <Onyx/Media/MediaSource.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

namespace Onyx::Media {

class AudioRing;

// Play state and clock shared by the calling thread, the decode thread and the audio callback.
// Commands change the state at once; a seek is only requested here (a serial and a target) and the
// decode thread reports it applied once the decoders, the mailbox and the ring were flushed. Until
// then Position() reports the target. The audio clock yields to the steady clock when the decode
// thread finds the audio exhausted while playing. Thread-safe.
class Transport {
public:
    struct SeekRequest {
        uint64_t serial = 0;
        double target = 0;
    };

    // `audio` is the ring whose consumed frames are the clock, or null for the steady clock.
    void Configure(double duration, const AudioRing* audio, uint32_t rate);
    // Hands the clock to the steady clock from the current audio position, so Position() stays
    // monotonic; the next applied seek returns to the audio clock.
    void UseSteadyClock();
    bool UsesAudioClock() const;

    void Play();
    void Pause();
    void Stop();
    void Seek(double seconds);

    PlayState State() const;
    double Position() const;
    uint64_t Serial() const;
    // True while the audio callback may consume samples.
    bool Running() const { return m_running.load(std::memory_order_acquire); }

    std::optional<SeekRequest> Pending() const;
    void Applied(uint64_t serial);
    // Playing becomes Ended; any other state is kept.
    void End();

private:
    using Clock = std::chrono::steady_clock;

    double PositionLocked(Clock::time_point now) const;
    void RequestSeekLocked(double target);
    void SetStateLocked(PlayState state);

    mutable std::mutex m_mutex;
    PlayState m_state = PlayState::Stopped;
    double m_duration = 0;
    // The configured ring, and the one the clock follows now (null while on the steady clock).
    const AudioRing* m_ring = nullptr;
    const AudioRing* m_audio = nullptr;
    uint32_t m_rate = 0;
    // The position when the steady clock started, or when the ring last counted zero frames.
    double m_base = 0;
    Clock::time_point m_start = Clock::now();
    uint64_t m_serial = 0;
    uint64_t m_applied = 0;
    std::atomic<bool> m_running{false};
};

// Decoded frames waiting for the calling thread: the newest frame already due plus at most
// kAhead frames ahead of the clock. Frames carry the seek serial they were decoded under, and a frame
// of an older serial is never handed out. Thread-safe.
class FrameMailbox {
public:
    static constexpr size_t kAhead = 4;

    // Drops every frame due at `position` except the newest; true when a frame fits.
    bool HasRoom(double position, double tolerance);
    void Push(VideoFrame frame, uint64_t serial);
    std::optional<VideoFrame> Take(double position, double tolerance, uint64_t serial);
    size_t Ahead(double position, double tolerance) const;
    size_t Size() const;
    void Clear();

private:
    struct Slot {
        uint64_t serial;
        VideoFrame frame;
    };

    mutable std::mutex m_mutex;
    std::deque<Slot> m_slots;
};

}
