#include "Onyx/Media/AudioOutput.h"

#include "PlaybackDevice.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <mutex>
#include <utility>

namespace Onyx::Media {

// `mutex` guards the clip and the cursor, which the device callback reads; `deviceMutex` serialises
// opening, starting, stopping and closing the device, which happens without `mutex` held because
// stopping and closing wait for the callback.
struct AudioOutput::Impl {
    mutable std::mutex mutex;
    Domain::Audio clip;
    size_t cursor = 0;
    bool playing = false;
    bool paused = false;

    mutable std::mutex deviceMutex;
    PlaybackDevice device;

    size_t Frames() const { return clip.channels == 0 ? 0 : clip.samples.size() / clip.channels; }

    void Fill(float* out, uint32_t frames, uint32_t channels) {
        size_t copied = 0;
        {
            std::lock_guard lock(mutex);
            if (playing && !paused && clip.channels == channels) {
                copied = std::min<size_t>(frames, Frames() - std::min(cursor, Frames()));
                const float* from = clip.samples.data() + cursor * channels;
                std::copy(from, from + copied * channels, out);
                cursor += copied;
                if (cursor >= Frames()) playing = false;
            }
        }
        std::fill(out + copied * channels, out + static_cast<size_t>(frames) * channels, 0.0f);
    }
};

AudioOutput::AudioOutput() : m_impl(std::make_unique<Impl>()) {}

AudioOutput::~AudioOutput() {
    std::lock_guard lock(m_impl->deviceMutex);
    m_impl->device.Close();
}

Result<void> AudioOutput::Play(const Domain::Audio& audio) {
    if (audio.sampleRate == 0 || audio.channels == 0 || audio.samples.size() < audio.channels) {
        return Result<void>::Fail({"media.audio", "the clip has no sample rate, channels or samples"});
    }
    // Copied before taking `mutex`, so the callback never waits for the copy.
    Domain::Audio clip = audio;
    {
        std::lock_guard lock(m_impl->mutex);
        m_impl->playing = false;
    }
    {
        std::lock_guard lock(m_impl->deviceMutex);
        PlaybackDevice& device = m_impl->device;
        if (!device.IsOpen() || device.Rate() != audio.sampleRate || device.Channels() != audio.channels) {
            Impl* impl = m_impl.get();
            const uint32_t channels = audio.channels;
            auto opened = device.Open(audio.sampleRate, channels,
                                      [impl, channels](float* out, uint32_t frames) { impl->Fill(out, frames, channels); });
            if (!opened.ok()) return opened;
        } else if (auto started = device.Start(); !started.ok()) {
            return started;
        }
    }
    {
        std::lock_guard lock(m_impl->mutex);
        std::swap(m_impl->clip, clip);
        m_impl->cursor = 0;
        m_impl->paused = false;
        m_impl->playing = true;
    }
    return Result<void>::Ok();
}

void AudioOutput::Stop() {
    {
        std::lock_guard lock(m_impl->mutex);
        m_impl->playing = false;
        m_impl->paused = false;
        m_impl->cursor = 0;
    }
    std::lock_guard lock(m_impl->deviceMutex);
    m_impl->device.Stop();
}

void AudioOutput::Pause() {
    std::lock_guard lock(m_impl->mutex);
    if (m_impl->playing) m_impl->paused = true;
}

void AudioOutput::Resume() {
    std::lock_guard lock(m_impl->mutex);
    m_impl->paused = false;
}

void AudioOutput::Seek(double seconds) {
    std::lock_guard lock(m_impl->mutex);
    if (m_impl->clip.sampleRate == 0) return;
    const double frame = std::max(seconds, 0.0) * m_impl->clip.sampleRate;
    m_impl->cursor = std::min(static_cast<size_t>(std::llround(frame)), m_impl->Frames());
}

bool AudioOutput::Paused() const {
    std::lock_guard lock(m_impl->mutex);
    return m_impl->playing && m_impl->paused;
}

bool AudioOutput::Playing() const {
    std::lock_guard lock(m_impl->mutex);
    return m_impl->playing;
}

double AudioOutput::Latency() const {
    std::lock_guard lock(m_impl->deviceMutex);
    return m_impl->device.Latency();
}

bool AudioOutput::DeviceRunning() const {
    std::lock_guard lock(m_impl->deviceMutex);
    return m_impl->device.Running();
}

double AudioOutput::Position() const {
    std::lock_guard lock(m_impl->mutex);
    if (m_impl->clip.sampleRate == 0) return 0.0;
    return static_cast<double>(m_impl->cursor) / m_impl->clip.sampleRate;
}

}
