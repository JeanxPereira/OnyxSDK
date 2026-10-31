#pragma once

#include "AvioFile.h"
#include "Ffmpeg.h"

#include <Onyx/Media/MediaSource.h>

#include <cstddef>
#include <deque>
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

    // Reads one packet and hands every frame it completes to the sinks. With `holdVideo`, or while
    // packets are held, a video packet is queued undecoded instead, so the reader can reach audio
    // that lies behind the video in the file without decoding more pictures. At the end of the
    // container, or when a read fails (logged as media.read), it drains the audio decoder and
    // returns false; later calls return false until the next Seek.
    bool Feed(const VideoSink& video, const AudioSink& audio, bool holdVideo = false);
    // Decodes the oldest held video packet, or drains the video decoder once the container ended and
    // nothing is held. False when there was nothing to do.
    bool DecodeHeld(const VideoSink& video);
    // The container ended; every audio frame was delivered.
    bool ReadEnded() const { return m_readEnded; }
    // Every frame of both streams was delivered.
    bool Ended() const { return m_readEnded && m_held.empty() && m_videoDrained; }
    size_t HeldCount() const { return m_held.size(); }
    size_t HeldBytes() const { return m_heldBytes; }
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
    std::deque<PacketPtr> m_held;
    size_t m_heldBytes = 0;
    bool m_readEnded = false;
    bool m_videoDrained = false;
};

}
