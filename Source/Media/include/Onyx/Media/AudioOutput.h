#pragma once

#include <Onyx/Core/Domain/Audio.h>
#include <Onyx/Core/Result.h>
#include <Onyx/Ui/AudioOutput.h>

#include <memory>

// Error codes (Result<T>::error().code):
//   media.audio  the clip has no sample rate, channels or samples, or no playback device could be
//                opened or started
namespace Onyx::Media {

// Plays decoded clips on one playback device (float32 at the clip's rate and channel count; the device
// is opened again when a clip with another rate or channel count plays). Position() counts the frames
// the device consumed. Thread-safe.
class AudioOutput final : public Ui::IAudioOutput {
public:
    AudioOutput();
    ~AudioOutput() override;

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    Result<void> Play(const Domain::Audio& audio) override;
    void Stop() override;
    void Pause() override;
    void Resume() override;
    void Seek(double seconds) override;
    bool Paused() const override;
    // False once the device consumed the whole clip.
    bool Playing() const override;
    double Position() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
