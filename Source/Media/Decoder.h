#pragma once

#include "AvioFile.h"
#include "Ffmpeg.h"

#include <Onyx/Media/MediaSource.h>

#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace Onyx::Media {

// The demuxer and the two decoders of one clip. Lives on the decode thread only.
class Decoder {
public:
    using VideoSink = std::function<void(VideoFrame)>;
    // Interleaved float32 at SampleRate() and Channels(), starting at `pts` seconds (negative when unknown).
    using AudioSink = std::function<void(std::span<const float> samples, double pts)>;

    Result<MediaInfo> Open(std::shared_ptr<const Vfs::IFile> file, uint64_t offset, uint64_t size);

    // Reads one packet and hands every frame it completes to the sinks. At the end of the container
    // it drains both decoders and returns false; later calls return false until the next Seek.
    bool Feed(const VideoSink& video, const AudioSink& audio);
    // Seeks to the key frame at or before `seconds` and flushes both decoders.
    Result<void> Seek(double seconds);
    // Audio packets are skipped from now on.
    void DisableAudio();

    bool AudioEnabled() const { return m_audio != nullptr; }
    uint32_t SampleRate() const { return m_sampleRate; }
    uint32_t Channels() const { return m_channels; }

private:
    Result<void> OpenVideo(MediaInfo& info);
    Result<void> OpenAudio(MediaInfo& info);
    void ReceiveVideo(const VideoSink& sink);
    void ReceiveAudio(const AudioSink& sink);
    double Seconds(int64_t ts, AVRational base) const;

    // Declared first, destroyed last: the format context reads through it until it closes.
    std::unique_ptr<AvioFile> m_avio;
    InputPtr m_input;
    CodecPtr m_video;
    CodecPtr m_audio;
    ScalePtr m_scale;
    ResamplePtr m_resample;
    PacketPtr m_packet;
    FramePtr m_frame;
    int m_videoIndex = -1;
    int m_audioIndex = -1;
    double m_startTime = 0;
    double m_frameDuration = 0;
    double m_nextVideoPts = 0;
    uint32_t m_sampleRate = 0;
    uint32_t m_channels = 0;
    std::vector<float> m_samples;
    bool m_ended = false;
};

}
