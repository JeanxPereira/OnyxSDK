#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswscale/swscale.h>
}

#include <Onyx/Media/MediaSource.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

// A clip encoded with FFmpeg's own encoders: mpeg1video 64x48 at 25 fps, frame i a solid colour
// Expected(i), muxed to AVI (with a pcm_s16le 44100 Hz stereo 440 Hz tone when asked) and written
// after 4096 zero bytes, so the clip lives at [offset, offset + size) of `path`. A positive
// `audioSeconds` cuts the tone short of the video; zero lasts as long as the video.
struct ClipFixture {
    std::filesystem::path path;
    uint64_t offset;
    uint64_t size;
};

inline std::array<uint8_t, 3> Expected(uint32_t i) {
    return {static_cast<uint8_t>(i * 5), static_cast<uint8_t>(255 - i * 5), 128};
}

inline std::array<uint8_t, 3> CentreColour(const Onyx::Media::VideoFrame& frame) {
    const size_t at = (static_cast<size_t>(frame.height / 2) * frame.width + frame.width / 2) * 4;
    return {static_cast<uint8_t>(frame.rgba[at]), static_cast<uint8_t>(frame.rgba[at + 1]),
            static_cast<uint8_t>(frame.rgba[at + 2])};
}

namespace ClipDetail {

constexpr int kWidth = 64;
constexpr int kHeight = 48;
constexpr int kFps = 25;
constexpr int kRate = 44100;
constexpr int kSamplesPerFrame = kRate / kFps;
constexpr uint64_t kPadding = 4096;

inline void Check(int result, const char* what) {
    if (result < 0) {
        char text[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(result, text, sizeof(text));
        throw std::runtime_error(std::string(what) + ": " + text);
    }
}

inline void Drain(AVFormatContext* out, AVCodecContext* codec, AVStream* stream, AVPacket* packet) {
    for (;;) {
        const int r = avcodec_receive_packet(codec, packet);
        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return;
        Check(r, "receive packet");
        av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
        packet->stream_index = stream->index;
        Check(av_interleaved_write_frame(out, packet), "write packet");
    }
}

}

inline ClipFixture WriteClip(const std::string& name, bool withAudio, uint32_t frames = 50, double audioSeconds = 0) {
    using namespace ClipDetail;
    const std::filesystem::path dir = std::filesystem::temp_directory_path();
    const std::filesystem::path raw = dir / ("onyx-media-" + name + ".raw.avi");
    const std::filesystem::path path = dir / ("onyx-media-" + name + ".bin");
    const std::string rawName = raw.string();

    AVFormatContext* out = nullptr;
    Check(avformat_alloc_output_context2(&out, nullptr, "avi", rawName.c_str()), "output context");

    const AVCodec* videoCodec = avcodec_find_encoder(AV_CODEC_ID_MPEG1VIDEO);
    if (!videoCodec) throw std::runtime_error("no mpeg1video encoder");
    AVStream* videoStream = avformat_new_stream(out, nullptr);
    AVCodecContext* video = avcodec_alloc_context3(videoCodec);
    video->width = kWidth;
    video->height = kHeight;
    video->time_base = AVRational{1, kFps};
    video->framerate = AVRational{kFps, 1};
    video->pix_fmt = AV_PIX_FMT_YUV420P;
    video->gop_size = 12;
    video->max_b_frames = 0;
    video->flags |= AV_CODEC_FLAG_QSCALE;
    video->global_quality = FF_QP2LAMBDA * 2;
    if (out->oformat->flags & AVFMT_GLOBALHEADER) video->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    Check(avcodec_open2(video, videoCodec, nullptr), "open mpeg1video");
    Check(avcodec_parameters_from_context(videoStream->codecpar, video), "video parameters");
    videoStream->time_base = video->time_base;

    AVStream* audioStream = nullptr;
    AVCodecContext* audio = nullptr;
    if (withAudio) {
        const AVCodec* audioCodec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
        if (!audioCodec) throw std::runtime_error("no pcm_s16le encoder");
        audioStream = avformat_new_stream(out, nullptr);
        audio = avcodec_alloc_context3(audioCodec);
        audio->sample_rate = kRate;
        audio->sample_fmt = AV_SAMPLE_FMT_S16;
        av_channel_layout_default(&audio->ch_layout, 2);
        audio->time_base = AVRational{1, kRate};
        Check(avcodec_open2(audio, audioCodec, nullptr), "open pcm_s16le");
        Check(avcodec_parameters_from_context(audioStream->codecpar, audio), "audio parameters");
        audioStream->time_base = audio->time_base;
    }

    Check(avio_open(&out->pb, rawName.c_str(), AVIO_FLAG_WRITE), "open output");
    Check(avformat_write_header(out, nullptr), "write header");

    AVPacket* packet = av_packet_alloc();
    AVFrame* picture = av_frame_alloc();
    picture->format = AV_PIX_FMT_YUV420P;
    picture->width = kWidth;
    picture->height = kHeight;
    Check(av_frame_get_buffer(picture, 0), "picture buffer");
    SwsContext* scale = sws_getContext(kWidth, kHeight, AV_PIX_FMT_RGB24, kWidth, kHeight, AV_PIX_FMT_YUV420P,
                                       SWS_BILINEAR, nullptr, nullptr, nullptr);
    std::vector<uint8_t> rgb(static_cast<size_t>(kWidth) * kHeight * 3);

    AVFrame* tone = nullptr;
    if (audio) {
        tone = av_frame_alloc();
        tone->format = AV_SAMPLE_FMT_S16;
        tone->sample_rate = kRate;
        tone->nb_samples = kSamplesPerFrame;
        av_channel_layout_default(&tone->ch_layout, 2);
        Check(av_frame_get_buffer(tone, 0), "tone buffer");
    }

    const uint32_t audioFrames =
        audioSeconds > 0 ? static_cast<uint32_t>(std::lround(audioSeconds * kFps)) : frames;
    for (uint32_t i = 0; i < frames; ++i) {
        const std::array<uint8_t, 3> colour = Expected(i);
        for (size_t p = 0; p < rgb.size(); p += 3) {
            rgb[p] = colour[0];
            rgb[p + 1] = colour[1];
            rgb[p + 2] = colour[2];
        }
        Check(av_frame_make_writable(picture), "picture writable");
        const uint8_t* src[1] = {rgb.data()};
        const int srcStride[1] = {kWidth * 3};
        sws_scale(scale, src, srcStride, 0, kHeight, picture->data, picture->linesize);
        picture->pts = i;
        Check(avcodec_send_frame(video, picture), "send picture");
        Drain(out, video, videoStream, packet);

        if (audio && i < audioFrames) {
            Check(av_frame_make_writable(tone), "tone writable");
            auto* samples = reinterpret_cast<int16_t*>(tone->data[0]);
            for (int s = 0; s < kSamplesPerFrame; ++s) {
                const double t = static_cast<double>(static_cast<int64_t>(i) * kSamplesPerFrame + s) / kRate;
                const auto value = static_cast<int16_t>(std::lround(8000.0 * std::sin(2.0 * 3.14159265358979 * 440.0 * t)));
                samples[s * 2] = value;
                samples[s * 2 + 1] = value;
            }
            tone->pts = static_cast<int64_t>(i) * kSamplesPerFrame;
            Check(avcodec_send_frame(audio, tone), "send tone");
            Drain(out, audio, audioStream, packet);
        }
    }
    Check(avcodec_send_frame(video, nullptr), "flush video");
    Drain(out, video, videoStream, packet);
    if (audio) {
        Check(avcodec_send_frame(audio, nullptr), "flush audio");
        Drain(out, audio, audioStream, packet);
    }
    Check(av_write_trailer(out), "write trailer");
    avio_closep(&out->pb);

    sws_freeContext(scale);
    av_frame_free(&tone);
    av_frame_free(&picture);
    av_packet_free(&packet);
    avcodec_free_context(&audio);
    avcodec_free_context(&video);
    avformat_free_context(out);

    std::vector<char> clip;
    {
        std::ifstream in(raw, std::ios::binary);
        clip.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::filesystem::remove(raw);
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        const std::vector<char> padding(kPadding, 0);
        file.write(padding.data(), static_cast<std::streamsize>(padding.size()));
        file.write(clip.data(), static_cast<std::streamsize>(clip.size()));
    }
    return ClipFixture{path, kPadding, static_cast<uint64_t>(clip.size())};
}
