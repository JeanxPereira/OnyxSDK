#include "AudioRing.h"

#include <algorithm>

namespace Onyx::Media {

void AudioRing::Reset(uint32_t channels, size_t capacityFrames) {
    std::lock_guard lock(m_mutex);
    m_channels = channels;
    m_samples.assign(static_cast<size_t>(channels) * capacityFrames, 0.0f);
    m_head = 0;
    m_count = 0;
    m_consumed.store(0, std::memory_order_release);
}

size_t AudioRing::Write(std::span<const float> samples) {
    std::lock_guard lock(m_mutex);
    if (m_channels == 0) return 0;
    const size_t room = (m_samples.size() - m_count) / m_channels * m_channels;
    const size_t take = std::min(room, samples.size() / m_channels * m_channels);
    const size_t capacity = m_samples.size();
    for (size_t i = 0; i < take; ++i) m_samples[(m_head + m_count + i) % capacity] = samples[i];
    m_count += take;
    return take;
}

size_t AudioRing::Read(float* out, size_t frames, float volume) {
    size_t read = 0;
    {
        std::lock_guard lock(m_mutex);
        if (m_channels != 0) {
            read = std::min(frames, m_count / m_channels);
            const size_t samples = read * m_channels;
            const size_t capacity = m_samples.size();
            for (size_t i = 0; i < samples; ++i) out[i] = m_samples[(m_head + i) % capacity] * volume;
            m_head = (m_head + samples) % std::max<size_t>(capacity, 1);
            m_count -= samples;
            m_consumed.fetch_add(read, std::memory_order_acq_rel);
        }
    }
    const size_t channels = std::max<uint32_t>(m_channels, 1);
    std::fill(out + read * channels, out + frames * channels, 0.0f);
    return read;
}

void AudioRing::Clear() {
    std::lock_guard lock(m_mutex);
    m_head = 0;
    m_count = 0;
    m_consumed.store(0, std::memory_order_release);
}

size_t AudioRing::BufferedFrames() const {
    std::lock_guard lock(m_mutex);
    return m_channels == 0 ? 0 : m_count / m_channels;
}

size_t AudioRing::FreeFrames() const {
    std::lock_guard lock(m_mutex);
    return m_channels == 0 ? 0 : (m_samples.size() - m_count) / m_channels;
}

}
