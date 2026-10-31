#pragma once

#include "AudioRing.h"
#include "PlaybackDevice.h"
#include "Transport.h"

#include <Onyx/Media/MediaSource.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

namespace Onyx::Media {

class Decoder;

// The state one open clip shares between the calling thread, the decode thread and the audio callback.
// The decode thread owns the Decoder (every FFmpeg object) and the playback device; the rest is
// thread-safe on its own. `wakeups` and `quit` are guarded by `wakeMutex`.
struct MediaSource::Impl {
    Transport transport;
    FrameMailbox mailbox;
    AudioRing ring;
    std::atomic<float> volume{1.0f};
    // Half a frame: a frame is due once the clock is within it of the frame's pts.
    double tolerance = 0.02;

    std::mutex wakeMutex;
    std::condition_variable wake;
    uint64_t wakeups = 0;
    bool quit = false;
    std::thread thread;

    ~Impl();

    void Start(std::shared_ptr<const Vfs::IFile> file, uint64_t offset, uint64_t size,
               std::promise<Result<MediaInfo>> opened);
    void Wake();

private:
    // Decode-thread state.
    struct Session {
        MediaInfo info;
        bool audioClock = false;
        uint32_t rate = 0;
        bool eof = false;
        uint64_t serial = 0;
        double dropBefore = 0;
        std::vector<float> pendingAudio;
    };

    void Run(std::shared_ptr<const Vfs::IFile> file, uint64_t offset, uint64_t size,
             std::promise<Result<MediaInfo>> opened);
    void Loop(Decoder& decoder, Session& session);
    void ApplySeek(Decoder& decoder, Session& session, const Transport::SeekRequest& seek);
    void PushAudio(Session& session, std::span<const float> samples, double pts);
    // The audio clock can advance no further: the ring ran dry and the thread cannot read on, because
    // the container ended or the video ahead of the clock fills the mailbox.
    bool AudioExhausted(const Session& session) const;
    bool Finished(const Session& session, double position) const;

    PlaybackDevice m_device;
};

}
