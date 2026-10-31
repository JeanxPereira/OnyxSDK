#pragma once

#include <Onyx/Core/Domain/Audio.h>
#include <Onyx/Core/Result.h>

namespace Onyx::Ui {

// Plays decoded audio. The Ui layer only drives it; a host supplies the implementation and keeps it
// alive for as long as any UiContext points at it.
class IAudioOutput {
public:
    virtual ~IAudioOutput() = default;

    virtual Result<void> Play(const Domain::Audio& audio) = 0;
    virtual void Stop() = 0;
    // Holds the clip and its position; Playing() stays true while paused.
    virtual void Pause() = 0;
    virtual void Resume() = 0;
    // Moves the playback position of the current clip, clamped to its length.
    virtual void Seek(double seconds) = 0;
    virtual bool Paused() const = 0;
    virtual bool Playing() const = 0;
    // Seconds since the current clip started; holds during a pause.
    virtual double Position() const = 0;
};

}
