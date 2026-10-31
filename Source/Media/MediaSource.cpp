#include "Onyx/Media/MediaSource.h"

#include "Decoder.h"
#include "MediaSourceImpl.h"

#include <Onyx/Core/Services/Log.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace Onyx::Media {

namespace {

// Seconds of decoded audio the ring holds ahead of the device.
constexpr double kRingSeconds = 1.0;
constexpr auto kPlayingPoll = std::chrono::milliseconds(5);
// How long the audio clock may stay starved, with the thread unable to read on, before the steady
// clock takes over.
constexpr auto kStarvationWindow = std::chrono::milliseconds(250);
// The compressed video the thread may hold while it reads ahead to audio that lags the video in the file.
constexpr size_t kMaxHeldPackets = 256;
constexpr size_t kMaxHeldBytes = 32u << 20;

}

MediaSource::Impl::~Impl() {
    {
        std::lock_guard lock(wakeMutex);
        quit = true;
    }
    wake.notify_all();
    if (thread.joinable()) thread.join();
}

void MediaSource::Impl::Wake() {
    {
        std::lock_guard lock(wakeMutex);
        ++wakeups;
    }
    wake.notify_all();
}

void MediaSource::Impl::Start(std::shared_ptr<const Vfs::IFile> file, uint64_t offset, uint64_t size,
                              std::promise<Result<MediaInfo>> opened) {
    thread = std::thread(&Impl::Run, this, std::move(file), offset, size, std::move(opened));
}

void MediaSource::Impl::Run(std::shared_ptr<const Vfs::IFile> file, uint64_t offset, uint64_t size,
                            std::promise<Result<MediaInfo>> opened) {
    Decoder decoder;
    auto info = decoder.Open(std::move(file), offset, size);
    if (!info.ok()) {
        opened.set_value(std::move(info));
        return;
    }
    Session session;
    session.info = info.value();
    tolerance = session.info.fps > 0 ? 0.5 / session.info.fps : 0.02;

    if (decoder.AudioEnabled()) {
        const uint32_t rate = decoder.SampleRate();
        const uint32_t channels = decoder.Channels();
        ring.Reset(channels, static_cast<size_t>(rate * kRingSeconds));
        auto started = m_device.Open(rate, channels, [this, channels](float* out, uint32_t frames) {
            if (transport.Running()) {
                ring.Read(out, frames, volume.load(std::memory_order_relaxed));
            } else {
                std::fill(out, out + static_cast<size_t>(frames) * channels, 0.0f);
            }
        });
        if (started.ok()) {
            session.audioClock = true;
            session.rate = rate;
            transport.Configure(session.info.duration, &ring, rate);
        } else {
            ONYX_LOG_WARN("media", "{}: {}; the clip plays silent on the steady clock", started.error().code,
                          started.error().message);
            decoder.DisableAudio();
            transport.Configure(session.info.duration, nullptr, 0);
        }
    } else {
        transport.Configure(session.info.duration, nullptr, 0);
    }

    opened.set_value(std::move(info));
    Loop(decoder, session);
    m_device.Close();
}

void MediaSource::Impl::Loop(Decoder& decoder, Session& session) {
    const Decoder::VideoSink video = [&](VideoFrame frame) {
        if (frame.pts < session.dropBefore - tolerance) return;
        mailbox.Push(std::move(frame), session.serial);
    };
    const Decoder::AudioSink audio = [&](std::span<const float> samples, double pts) {
        PushAudio(session, samples, pts);
    };

    for (;;) {
        uint64_t seen = 0;
        {
            std::lock_guard lock(wakeMutex);
            if (quit) return;
            seen = wakeups;
        }
        if (const auto seek = transport.Pending()) {
            ApplySeek(decoder, session, *seek);
            continue;
        }
        if (!session.pendingAudio.empty()) {
            const size_t written = ring.Write(session.pendingAudio);
            session.pendingAudio.erase(session.pendingAudio.begin(),
                                       session.pendingAudio.begin() + static_cast<std::ptrdiff_t>(written));
        }

        const double position = transport.Position();
        const bool playing = transport.State() == PlayState::Playing;
        const bool videoRoom = !session.info.hasVideo || mailbox.HasRoom(position, tolerance);
        if (videoRoom && decoder.DecodeHeld(video)) {
            session.starvedSince.reset();
            continue;
        }
        if (session.pendingAudio.empty() && !decoder.ReadEnded() &&
            (videoRoom || ReadAhead(decoder, session, playing))) {
            decoder.Feed(video, audio, !videoRoom);
            session.starvedSince.reset();
            continue;
        }
        if (playing && AudioExhausted(session)) {
            const auto now = std::chrono::steady_clock::now();
            if (!session.starvedSince) session.starvedSince = now;
            if (decoder.ReadEnded() || now - *session.starvedSince >= kStarvationWindow) {
                transport.UseSteadyClock();
                session.starvedSince.reset();
                continue;
            }
        } else {
            session.starvedSince.reset();
        }
        if (decoder.Ended() && playing && Finished(session, position)) {
            transport.End();
            continue;
        }

        std::unique_lock lock(wakeMutex);
        const auto woken = [&] { return quit || wakeups != seen; };
        if (playing) {
            wake.wait_for(lock, kPlayingPoll, woken);
        } else {
            wake.wait(lock, woken);
        }
    }
}

void MediaSource::Impl::ApplySeek(Decoder& decoder, Session& session, const Transport::SeekRequest& seek) {
    if (auto sought = decoder.Seek(seek.target); !sought.ok()) {
        ONYX_LOG_WARN("media", "{}: {}", sought.error().code, sought.error().message);
    }
    mailbox.Clear();
    ring.Clear();
    session.pendingAudio.clear();
    session.starvedSince.reset();
    session.dropBefore = seek.target;
    session.serial = seek.serial;
    transport.Applied(seek.serial);
}

void MediaSource::Impl::PushAudio(Session& session, std::span<const float> samples, double pts) {
    if (!session.audioClock) return;
    const uint32_t channels = ring.Channels();
    if (pts >= 0 && pts < session.dropBefore && channels != 0) {
        const auto late = static_cast<size_t>((session.dropBefore - pts) * session.rate + 0.5);
        samples = samples.subspan(std::min(late * channels, samples.size()));
    }
    const size_t written = session.pendingAudio.empty() ? ring.Write(samples) : 0;
    session.pendingAudio.insert(session.pendingAudio.end(), samples.begin() + static_cast<std::ptrdiff_t>(written),
                                samples.end());
}

bool MediaSource::Impl::ReadAhead(const Decoder& decoder, const Session& session, bool playing) const {
    return playing && transport.UsesAudioClock() && ring.BufferedFrames() < std::max<uint32_t>(session.rate / 10, 1) &&
           decoder.HeldCount() < kMaxHeldPackets && decoder.HeldBytes() < kMaxHeldBytes;
}

bool MediaSource::Impl::AudioExhausted(const Session& session) const {
    return transport.UsesAudioClock() && session.pendingAudio.empty() && ring.BufferedFrames() == 0;
}

bool MediaSource::Impl::Finished(const Session& session, double position) const {
    if (!session.pendingAudio.empty() || transport.UsesAudioClock()) return false;
    if (mailbox.Ahead(position, tolerance) > 0) return false;
    return session.info.duration <= 0 || position >= session.info.duration - 1e-6;
}

MediaSource::MediaSource() : m_impl(std::make_unique<Impl>()) {}

MediaSource::~MediaSource() = default;

Result<MediaInfo> MediaSource::Open(std::shared_ptr<const Vfs::IFile> file, uint64_t offset, uint64_t size) {
    const float volume = Volume();
    m_impl = std::make_unique<Impl>();
    m_impl->volume.store(volume, std::memory_order_relaxed);
    m_info = MediaInfo{};
    if (size == 0 && file && offset <= file->Size()) size = file->Size() - offset;

    std::promise<Result<MediaInfo>> promise;
    auto future = promise.get_future();
    m_impl->Start(std::move(file), offset, size, std::move(promise));
    Result<MediaInfo> info = future.get();
    if (!info.ok()) {
        m_impl = std::make_unique<Impl>();
        m_impl->volume.store(volume, std::memory_order_relaxed);
        return info;
    }
    m_info = info.value();
    return info;
}

void MediaSource::Play() {
    m_impl->transport.Play();
    m_impl->Wake();
}

void MediaSource::Pause() {
    m_impl->transport.Pause();
    m_impl->Wake();
}

void MediaSource::Stop() {
    m_impl->transport.Stop();
    m_impl->Wake();
}

void MediaSource::Seek(double seconds) {
    m_impl->transport.Seek(seconds);
    m_impl->Wake();
}

PlayState MediaSource::State() const {
    return m_impl->transport.State();
}

double MediaSource::Position() const {
    return m_impl->transport.Position();
}

const MediaInfo& MediaSource::Info() const {
    return m_info;
}

std::optional<VideoFrame> MediaSource::TakeFrame() {
    const double position = m_impl->transport.Position();
    auto frame = m_impl->mailbox.Take(position, m_impl->tolerance, m_impl->transport.Serial());
    if (frame) m_impl->Wake();
    return frame;
}

void MediaSource::SetVolume(float volume) {
    m_impl->volume.store(std::clamp(volume, 0.0f, 1.0f), std::memory_order_relaxed);
}

bool MediaSource::UsesAudioClock() const {
    return m_impl->transport.UsesAudioClock();
}

float MediaSource::Volume() const {
    return m_impl->volume.load(std::memory_order_relaxed);
}

}
