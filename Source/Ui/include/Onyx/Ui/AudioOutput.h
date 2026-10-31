#pragma once

#include <Onyx/Core/Domain/Audio.h>
#include <Onyx/Core/Result.h>

#include <string_view>

namespace Onyx::Ui {

// Plays decoded audio. The Ui layer only drives it; a host supplies the implementation and keeps it
// alive for as long as any UiContext points at it.
//
// One output serves every view of a context. Play names the view that starts the clip (its Id());
// a Play from another owner takes the output over. A view treats the output's state (Playing,
// Paused, Position) as its own only while Owner() names it.
class IAudioOutput {
public:
    virtual ~IAudioOutput() = default;

    virtual Result<void> Play(const Domain::Audio& audio, std::string_view owner) = 0;
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
    // The owner the current clip was started for; empty when nothing plays (after Stop, a failed Play
    // or the end of the clip). Valid until the next Play or Stop.
    virtual std::string_view Owner() const = 0;
};

}
