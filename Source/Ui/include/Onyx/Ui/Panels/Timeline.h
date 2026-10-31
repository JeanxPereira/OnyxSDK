#pragma once

#include <Onyx/Ui/Panel.h>

#include <string_view>

namespace Onyx::Ui {

// A scrubber over the active clip: frame ticks on the draw list, a playhead, and a drag that seeks.
class Timeline : public IPanel {
public:
    // The time under pixel x of a track spanning [left, right], clamped to [0, duration].
    static float TimeAt(float x, float left, float right, float duration);
    // The pixel of `time` on that track; the inverse of TimeAt inside the clip.
    static float PixelAt(float time, float left, float right, float duration);

    std::string_view Id() const override { return "onyx.timeline"; }
    std::string_view Title() const override { return "Timeline"; }
    void Draw(UiContext& ui) override;
};

}
