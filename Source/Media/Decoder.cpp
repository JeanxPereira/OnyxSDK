#include "Decoder.h"

#include <Onyx/Core/Services/Log.h>

#include <algorithm>
#include <climits>
#include <string>
#include <utility>

namespace Onyx::Media {

namespace {

Result<CodecPtr> OpenCodec(const AVStream* stream) {
    using Out = Result<CodecPtr>;
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        return Out::Fail({"media.stream", std::string("no decoder for ") + avcodec_get_name(stream->codecpar->codec_id)});
    }
    CodecPtr context(avcodec_alloc_context3(codec));
    if (!context) return Out::Fail({"media.stream", "out of memory for a decoder"});
    int r = avcodec_parameters_to_context(context.get(), stream->codecpar);
    if (r >= 0) {
        context->pkt_timebase = stream->time_base;
        r = avcodec_open2(context.get(), codec, nullptr);
    }
    if (r < 0) {
        return Out::Fail({"media.stream", std::string("cannot open the ") + codec->name + " decoder: " + AvErrorText(r), r});
    }
    return Out::Ok(std::move(context));
}

}

Result<MediaInfo> Decoder::Open(std::shared_ptr<const Vfs::IFile> file, uint64_t offset, uint64_t size) {
    using Out = Result<MediaInfo>;
    auto avio = AvioFile::Create(std::move(file), offset, size);
    if (!avio.ok()) return Out::Fail(avio.error());
    m_avio = std::move(avio.value());

    AVFormatContext* raw = avformat_alloc_context();
    if (!raw) return Out::Fail({"media.open", "out of memory for the container"});
    raw->pb = m_avio->Context();
    raw->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (const int r = avformat_open_input(&raw, nullptr, nullptr, nullptr); r < 0) {
        return Out::Fail({"media.open", "no readable container: " + AvErrorText(r), r});
    }
    m_input.reset(raw);
    if (const int r = avformat_find_stream_info(m_input.get(), nullptr); r < 0) {
        return Out::Fail({"media.open", "cannot read the stream layout: " + AvErrorText(r), r});
    }
    m_videoIndex = av_find_best_stream(m_input.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    m_audioIndex = av_find_best_stream(m_input.get(), AVMEDIA_TYPE_AUDIO, -1, m_videoIndex, nullptr, 0);
    if (m_videoIndex < 0 && m_audioIndex < 0) return Out::Fail({"media.open", "the container holds no audio or video"});

    MediaInfo info;
    if (m_input->start_time != AV_NOPTS_VALUE) m_startTime = static_cast<double>(m_input->start_time) / AV_TIME_BASE;
    if (m_input->duration > 0) info.duration = static_cast<double>(m_input->duration) / AV_TIME_BASE;
    if (m_videoIndex >= 0) {
        if (auto r = OpenVideo(info); !r.ok()) return Out::Fail(r.error());
    }
    if (m_audioIndex >= 0) {
        if (auto r = OpenAudio(info); !r.ok()) return Out::Fail(r.error());
    }
    m_packet.reset(av_packet_alloc());
    m_frame.reset(av_frame_alloc());
    if (!m_packet || !m_frame) return Out::Fail({"media.open", "out of memory for packets"});
    return Out::Ok(std::move(info));
}

Result<void> Decoder::OpenVideo(MediaInfo& info) {
    AVStream* stream = m_input->streams[m_videoIndex];
    auto codec = OpenCodec(stream);
    if (!codec.ok()) return Result<void>::Fail(codec.error());
    m_video = std::move(codec.value());
    info.hasVideo = true;
    info.videoCodec = avcodec_get_name(stream->codecpar->codec_id);
    info.width = static_cast<uint32_t>(std::max(stream->codecpar->width, 0));
    info.height = static_cast<uint32_t>(std::max(stream->codecpar->height, 0));
    const AVRational rate = av_guess_frame_rate(m_input.get(), stream, nullptr);
    info.fps = rate.num > 0 && rate.den > 0 ? av_q2d(rate) : 0.0;
    m_frameDuration = info.fps > 0 ? 1.0 / info.fps : 0.04;
    AVRational sar = av_guess_sample_aspect_ratio(m_input.get(), stream, nullptr);
    if (sar.num <= 0 || sar.den <= 0) sar = AVRational{1, 1};
    if (info.height > 0) info.displayAspect = static_cast<double>(info.width) * av_q2d(sar) / info.height;
    if (info.duration <= 0 && stream->duration > 0) info.duration = Seconds(stream->duration, stream->time_base) + m_startTime;
    return Result<void>::Ok();
}

Result<void> Decoder::OpenAudio(MediaInfo& info) {
    AVStream* stream = m_input->streams[m_audioIndex];
    auto codec = OpenCodec(stream);
    if (!codec.ok()) return Result<void>::Fail(codec.error());
    m_audio = std::move(codec.value());
    info.hasAudio = true;
    info.audioCodec = avcodec_get_name(stream->codecpar->codec_id);
    m_sampleRate = static_cast<uint32_t>(std::max(m_audio->sample_rate, 0));
    m_channels = static_cast<uint32_t>(std::clamp(m_audio->ch_layout.nb_channels, 0, 8));

    AVChannelLayout out{};
    av_channel_layout_default(&out, static_cast<int>(m_channels));
    SwrContext* raw = nullptr;
    const int r = swr_alloc_set_opts2(&raw, &out, AV_SAMPLE_FMT_FLT, m_audio->sample_rate, &m_audio->ch_layout,
                                      m_audio->sample_fmt, m_audio->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&out);
    m_resample.reset(raw);
    if (r < 0 || swr_init(m_resample.get()) < 0) {
        return Result<void>::Fail({"media.stream", "cannot convert the " + info.audioCodec + " samples", r});
    }
    if (info.duration <= 0 && stream->duration > 0) info.duration = Seconds(stream->duration, stream->time_base) + m_startTime;
    return Result<void>::Ok();
}

double Decoder::Seconds(int64_t ts, AVRational base) const {
    return static_cast<double>(ts) * av_q2d(base) - m_startTime;
}

bool Decoder::Feed(const VideoSink& video, const AudioSink& audio, bool holdVideo) {
    if (m_readEnded) return false;
    int r = av_read_frame(m_input.get(), m_packet.get());
    // Some demuxers report a failed read as the end of the container; the IO context keeps the
    // cause.
    if (r == AVERROR_EOF && m_input->pb && m_input->pb->error < 0) r = m_input->pb->error;
    if (r < 0) {
        if (r != AVERROR_EOF) ONYX_LOG_WARN("media", "media.read: reading the container failed: {}", AvErrorText(r));
        if (m_audio && avcodec_send_packet(m_audio.get(), nullptr) >= 0) ReceiveAudio(audio);
        m_readEnded = true;
        m_videoDrained = !m_video;
        return false;
    }
    if (m_packet->stream_index == m_videoIndex && m_video) {
        if (holdVideo || !m_held.empty()) {
            PacketPtr held(av_packet_alloc());
            if (held) {
                av_packet_move_ref(held.get(), m_packet.get());
                m_heldBytes += static_cast<size_t>(std::max(held->size, 0));
                m_held.push_back(std::move(held));
            }
        } else if (avcodec_send_packet(m_video.get(), m_packet.get()) >= 0) {
            ReceiveVideo(video);
        }
    } else if (m_packet->stream_index == m_audioIndex && m_audio) {
        if (avcodec_send_packet(m_audio.get(), m_packet.get()) >= 0) ReceiveAudio(audio);
    }
    av_packet_unref(m_packet.get());
    return true;
}

bool Decoder::DecodeHeld(const VideoSink& video) {
    if (!m_held.empty()) {
        PacketPtr packet = std::move(m_held.front());
        m_held.pop_front();
        m_heldBytes -= std::min(m_heldBytes, static_cast<size_t>(std::max(packet->size, 0)));
        if (avcodec_send_packet(m_video.get(), packet.get()) >= 0) ReceiveVideo(video);
        return true;
    }
    if (m_readEnded && !m_videoDrained) {
        if (avcodec_send_packet(m_video.get(), nullptr) >= 0) ReceiveVideo(video);
        m_videoDrained = true;
        return true;
    }
    return false;
}

void Decoder::ReceiveVideo(const VideoSink& sink) {
    const AVRational base = m_input->streams[m_videoIndex]->time_base;
    while (avcodec_receive_frame(m_video.get(), m_frame.get()) >= 0) {
        AVFrame* f = m_frame.get();
        const int64_t ts = f->best_effort_timestamp;
        const double pts = ts != AV_NOPTS_VALUE ? Seconds(ts, base) : m_nextVideoPts;
        m_nextVideoPts = pts + m_frameDuration;
        m_scale.reset(sws_getCachedContext(m_scale.release(), f->width, f->height, static_cast<AVPixelFormat>(f->format),
                                           f->width, f->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr));
        if (m_scale && f->width > 0 && f->height > 0) {
            VideoFrame frame;
            frame.pts = pts;
            frame.width = static_cast<uint32_t>(f->width);
            frame.height = static_cast<uint32_t>(f->height);
            frame.rgba.resize(static_cast<size_t>(frame.width) * frame.height * 4);
            uint8_t* planes[4] = {reinterpret_cast<uint8_t*>(frame.rgba.data()), nullptr, nullptr, nullptr};
            const int strides[4] = {f->width * 4, 0, 0, 0};
            sws_scale(m_scale.get(), f->data, f->linesize, 0, f->height, planes, strides);
            sink(std::move(frame));
        }
        av_frame_unref(f);
    }
}

void Decoder::ReceiveAudio(const AudioSink& sink) {
    const AVRational base = m_input->streams[m_audioIndex]->time_base;
    while (avcodec_receive_frame(m_audio.get(), m_frame.get()) >= 0) {
        AVFrame* f = m_frame.get();
        const int64_t ts = f->best_effort_timestamp;
        const double pts = ts != AV_NOPTS_VALUE ? Seconds(ts, base) : -1.0;
        const int capacity = swr_get_out_samples(m_resample.get(), f->nb_samples);
        if (capacity > 0 && m_channels > 0) {
            m_samples.resize(static_cast<size_t>(capacity) * m_channels);
            uint8_t* out[1] = {reinterpret_cast<uint8_t*>(m_samples.data())};
            const int got = swr_convert(m_resample.get(), out, capacity, const_cast<const uint8_t**>(f->extended_data),
                                        f->nb_samples);
            if (got > 0) sink(std::span<const float>(m_samples.data(), static_cast<size_t>(got) * m_channels), pts);
        }
        av_frame_unref(f);
    }
}

Result<void> Decoder::Seek(double seconds) {
    const auto ts = static_cast<int64_t>((seconds + m_startTime) * AV_TIME_BASE);
    const int r = avformat_seek_file(m_input.get(), -1, INT64_MIN, ts, ts, 0);
    if (m_video) avcodec_flush_buffers(m_video.get());
    if (m_audio) {
        avcodec_flush_buffers(m_audio.get());
        swr_init(m_resample.get());
    }
    m_held.clear();
    m_heldBytes = 0;
    m_readEnded = false;
    m_videoDrained = false;
    m_nextVideoPts = seconds;
    if (r < 0) return Result<void>::Fail({"media.seek", "the container refused the seek: " + AvErrorText(r), r});
    return Result<void>::Ok();
}

void Decoder::DisableAudio() {
    m_audio.reset();
    m_resample.reset();
}

}
