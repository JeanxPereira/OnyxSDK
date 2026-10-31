#include "PlaybackDevice.h"

#ifndef MA_NO_DECODING
#define MA_NO_DECODING
#endif
#ifndef MA_NO_ENCODING
#define MA_NO_ENCODING
#endif
#ifndef MA_NO_GENERATION
#define MA_NO_GENERATION
#endif
#ifndef MA_NO_RESOURCE_MANAGER
#define MA_NO_RESOURCE_MANAGER
#endif
#ifndef MA_NO_NODE_GRAPH
#define MA_NO_NODE_GRAPH
#endif
#include <miniaudio.h>

#include <string>
#include <utility>

namespace Onyx::Media {

struct PlaybackDevice::Impl {
    ma_device device{};
    Fill fill;

    static void Callback(ma_device* device, void* output, const void*, ma_uint32 frames) {
        auto* self = static_cast<Impl*>(device->pUserData);
        self->fill(static_cast<float*>(output), frames);
    }
};

PlaybackDevice::PlaybackDevice() = default;

PlaybackDevice::~PlaybackDevice() {
    Close();
}

Result<void> PlaybackDevice::Open(uint32_t rate, uint32_t channels, Fill fill) {
    Close();
    if (rate == 0 || channels == 0) return Result<void>::Fail({"media.audio", "the clip has no sample rate or channels"});
    auto impl = std::make_unique<Impl>();
    impl->fill = std::move(fill);
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = channels;
    config.sampleRate = rate;
    config.dataCallback = &Impl::Callback;
    config.pUserData = impl.get();
    if (const ma_result r = ma_device_init(nullptr, &config, &impl->device); r != MA_SUCCESS) {
        return Result<void>::Fail({"media.audio", std::string("no playback device: ") + ma_result_description(r), r});
    }
    if (const ma_result r = ma_device_start(&impl->device); r != MA_SUCCESS) {
        ma_device_uninit(&impl->device);
        return Result<void>::Fail({"media.audio", std::string("the playback device did not start: ") + ma_result_description(r), r});
    }
    m_impl = std::move(impl);
    m_rate = rate;
    m_channels = channels;
    return Result<void>::Ok();
}

void PlaybackDevice::Close() {
    if (!m_impl) return;
    ma_device_uninit(&m_impl->device);
    m_impl.reset();
    m_rate = 0;
    m_channels = 0;
}

Result<void> PlaybackDevice::Start() {
    if (!m_impl || ma_device_is_started(&m_impl->device)) return Result<void>::Ok();
    if (const ma_result r = ma_device_start(&m_impl->device); r != MA_SUCCESS) {
        return Result<void>::Fail({"media.audio", std::string("the playback device did not start: ") + ma_result_description(r), r});
    }
    return Result<void>::Ok();
}

void PlaybackDevice::Stop() {
    if (m_impl && ma_device_is_started(&m_impl->device)) ma_device_stop(&m_impl->device);
}

bool PlaybackDevice::IsOpen() const {
    return m_impl != nullptr;
}

bool PlaybackDevice::Running() const {
    return m_impl && ma_device_is_started(&m_impl->device);
}

double PlaybackDevice::Latency() const {
    if (!m_impl) return 0.0;
    const auto& playback = m_impl->device.playback;
    const uint32_t rate = playback.internalSampleRate != 0 ? playback.internalSampleRate : m_impl->device.sampleRate;
    if (rate == 0) return 0.0;
    return static_cast<double>(playback.internalPeriodSizeInFrames) * playback.internalPeriods / rate;
}

}
