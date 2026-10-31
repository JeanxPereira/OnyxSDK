#pragma once

#include <Onyx/Render/AnimationPlayer.h>
#include <Onyx/Ui/Panel.h>
#include <Onyx/Ui/SceneSession.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Onyx::Ui {

// Play, pause and stop, a clip picker, speed and loop mode for the active scene tab. The clock that
// advances the clip is the Viewport's, so playback only moves while the Viewport panel is visible.
class Transport : public IPanel {
public:
    // What the panel shows, read from a session.
    struct Model {
        bool available = false;
        bool playing = false;
        // Views the session's clip names; valid while the session lives and is not rebound.
        std::span<const std::string> clips;
        uint32_t clip = 0;
        float time = 0.0f;
        float duration = 0.0f;
        float speed = 1.0f;
        Render::LoopMode loop = Render::LoopMode::Loop;
    };

    static Model Read(const SceneSession& session);

    std::string_view Id() const override { return "onyx.transport"; }
    std::string_view Title() const override { return "Transport"; }
    void Draw(UiContext& ui) override;
};

}
