#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <memory>
#include <string>

namespace Onyx::Media {

struct PacketDeleter {
    void operator()(AVPacket* packet) const { av_packet_free(&packet); }
};
struct FrameDeleter {
    void operator()(AVFrame* frame) const { av_frame_free(&frame); }
};
struct CodecDeleter {
    void operator()(AVCodecContext* codec) const { avcodec_free_context(&codec); }
};
struct InputDeleter {
    void operator()(AVFormatContext* format) const { avformat_close_input(&format); }
};
struct ScaleDeleter {
    void operator()(SwsContext* scale) const { sws_freeContext(scale); }
};
struct ResampleDeleter {
    void operator()(SwrContext* resample) const { swr_free(&resample); }
};

using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using CodecPtr = std::unique_ptr<AVCodecContext, CodecDeleter>;
using InputPtr = std::unique_ptr<AVFormatContext, InputDeleter>;
using ScalePtr = std::unique_ptr<SwsContext, ScaleDeleter>;
using ResamplePtr = std::unique_ptr<SwrContext, ResampleDeleter>;

inline std::string AvErrorText(int code) {
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof(text));
    return text;
}

}
