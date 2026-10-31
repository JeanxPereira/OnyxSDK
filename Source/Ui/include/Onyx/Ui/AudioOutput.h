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
    virtual bool Playing() const = 0;
    // Seconds since the current clip started.
    virtual double Position() const = 0;
};

}
