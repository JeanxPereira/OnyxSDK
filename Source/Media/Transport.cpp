#include "Transport.h"

#include "AudioRing.h"

#include <algorithm>
#include <utility>

namespace Onyx::Media {

void Transport::Configure(double duration, const AudioRing* audio, uint32_t rate) {
    std::lock_guard lock(m_mutex);
    m_duration = std::max(duration, 0.0);
    m_audio = rate != 0 ? audio : nullptr;
    m_rate = rate;
}

double Transport::PositionLocked(Clock::time_point now) const {
    double position = m_base;
    if (m_state == PlayState::Ended) {
        position = m_duration;
    } else if (m_serial == m_applied) {
        if (m_audio) {
            position += static_cast<double>(m_audio->Consumed()) / static_cast<double>(m_rate);
        } else if (m_state == PlayState::Playing) {
            position += std::chrono::duration<double>(now - m_start).count();
        }
    }
    if (m_duration > 0) position = std::min(position, m_duration);
    return std::max(position, 0.0);
}

void Transport::RequestSeekLocked(double target) {
    ++m_serial;
    m_base = target;
    m_start = Clock::now();
}

void Transport::SetStateLocked(PlayState state) {
    m_state = state;
    m_running.store(state == PlayState::Playing, std::memory_order_release);
}

void Transport::Play() {
    std::lock_guard lock(m_mutex);
    if (m_state == PlayState::Playing) return;
    if (m_state == PlayState::Ended) {
        m_state = PlayState::Stopped;
        RequestSeekLocked(0.0);
    }
    if (!m_audio) {
        m_base = PositionLocked(Clock::now());
        m_start = Clock::now();
    }
    SetStateLocked(PlayState::Playing);
}

void Transport::Pause() {
    std::lock_guard lock(m_mutex);
    if (m_state != PlayState::Playing) return;
    if (!m_audio) m_base = PositionLocked(Clock::now());
    SetStateLocked(PlayState::Paused);
}

void Transport::Stop() {
    std::lock_guard lock(m_mutex);
    SetStateLocked(PlayState::Stopped);
    RequestSeekLocked(0.0);
}

void Transport::Seek(double seconds) {
    std::lock_guard lock(m_mutex);
    double target = std::max(seconds, 0.0);
    if (m_duration > 0) target = std::min(target, m_duration);
    if (m_state == PlayState::Ended) SetStateLocked(PlayState::Paused);
    RequestSeekLocked(target);
}

PlayState Transport::State() const {
    std::lock_guard lock(m_mutex);
    return m_state;
}

double Transport::Position() const {
    std::lock_guard lock(m_mutex);
    return PositionLocked(Clock::now());
}

uint64_t Transport::Serial() const {
    std::lock_guard lock(m_mutex);
    return m_serial;
}

std::optional<Transport::SeekRequest> Transport::Pending() const {
    std::lock_guard lock(m_mutex);
    if (m_serial == m_applied) return std::nullopt;
    return SeekRequest{m_serial, m_base};
}

void Transport::Applied(uint64_t serial) {
    std::lock_guard lock(m_mutex);
    if (serial != m_serial) return;
    m_applied = serial;
    m_start = Clock::now();
}

void Transport::End() {
    std::lock_guard lock(m_mutex);
    if (m_state == PlayState::Playing) SetStateLocked(PlayState::Ended);
}

bool FrameMailbox::HasRoom(double position, double tolerance) {
    std::lock_guard lock(m_mutex);
    while (m_slots.size() >= 2 && m_slots[1].frame.pts <= position + tolerance) m_slots.pop_front();
    return m_slots.size() < 1 + kAhead;
}

void FrameMailbox::Push(VideoFrame frame, uint64_t serial) {
    std::lock_guard lock(m_mutex);
    m_slots.push_back(Slot{serial, std::move(frame)});
}

std::optional<VideoFrame> FrameMailbox::Take(double position, double tolerance, uint64_t serial) {
    std::lock_guard lock(m_mutex);
    while (!m_slots.empty() && m_slots.front().serial != serial) m_slots.pop_front();
    size_t due = 0;
    while (due < m_slots.size() && m_slots[due].frame.pts <= position + tolerance) ++due;
    if (due == 0) return std::nullopt;
    VideoFrame frame = std::move(m_slots[due - 1].frame);
    m_slots.erase(m_slots.begin(), m_slots.begin() + static_cast<std::ptrdiff_t>(due));
    return frame;
}

size_t FrameMailbox::Ahead(double position, double tolerance) const {
    std::lock_guard lock(m_mutex);
    return static_cast<size_t>(std::count_if(m_slots.begin(), m_slots.end(),
                                             [&](const Slot& s) { return s.frame.pts > position + tolerance; }));
}

size_t FrameMailbox::Size() const {
    std::lock_guard lock(m_mutex);
    return m_slots.size();
}

void FrameMailbox::Clear() {
    std::lock_guard lock(m_mutex);
    m_slots.clear();
}

}
