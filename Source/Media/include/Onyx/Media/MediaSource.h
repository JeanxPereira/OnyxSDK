#pragma once

#include <Onyx/Core/Result.h>
#include <Onyx/Core/Vfs/IFile.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Error codes (Result<T>::error().code):
//   media.open    the byte range is outside the file, or no container format with an audio or video
//                 stream could be read from it
//   media.stream  a stream's decoder is missing or failed to open
//   media.seek    logged when the container refuses a seek; decoding resumes wherever it stopped
namespace Onyx::Media {

struct MediaInfo {
    double duration = 0;
    uint32_t width = 0, height = 0;
    double fps = 0;
    double displayAspect = 0;
    std::string videoCodec, audioCodec;
    bool hasVideo = false, hasAudio = false;
};

// Tightly packed RGBA8, top row first.
struct VideoFrame {
    double pts = 0;
    uint32_t width = 0, height = 0;
    std::vector<std::byte> rgba;
};

enum class PlayState : uint8_t { Stopped, Playing, Paused, Ended };

// Decodes one clip on one decode thread. Every FFmpeg object is created, used and destroyed on that
// thread; the calling thread only queues commands and reads snapshots. Reads go through the IFile in
// [offset, offset + size), never through a path.
//
// The clock is the audio clock (frames the playback device consumed) when the clip has audio and a
// device started, a steady clock otherwise. Decoding runs ahead of the clock by at most four video
// frames and blocks there, so a paused source costs nothing. Methods other than Open may be called
// from any one thread at a time; the destructor stops and joins the decode thread.
class MediaSource {
public:
    MediaSource();
    ~MediaSource();

    MediaSource(const MediaSource&) = delete;
    MediaSource& operator=(const MediaSource&) = delete;

    // Closes the open clip first. A size of zero means up to the end of the file. Leaves the source
    // Stopped at zero with the first frame due.
    Result<MediaInfo> Open(std::shared_ptr<const Vfs::IFile> file, uint64_t offset, uint64_t size);

    // Play from Ended restarts from zero. Stop rewinds to zero.
    void Play();
    void Pause();
    void Stop();
    // Clamped to [0, duration]; lands on the first frame at or after `seconds`. Executed on the decode
    // thread; Position() reports the target meanwhile.
    void Seek(double seconds);

    PlayState State() const;
    double Position() const;
    const MediaInfo& Info() const;
    // The newest decoded frame due at Position(), dropping older ones; empty when nothing new is due.
    std::optional<VideoFrame> TakeFrame();

    // Linear gain in [0, 1] applied to the audio the device plays.
    void SetVolume(float volume);
    float Volume() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    MediaInfo m_info;
};

}
