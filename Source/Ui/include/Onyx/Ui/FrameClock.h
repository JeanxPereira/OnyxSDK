#pragma once

namespace Onyx::Ui {

// Decides whether the host must keep rendering. Animation requests are absolute deadlines, so a
// client asking every frame pushes the same deadline forward instead of accumulating time.
class FrameClock {
public:
    void RequestRedraw();
    void RequestAnimation(double seconds);

    // Returns whether this frame was asked for; consumes the single-shot redraw request.
    bool BeginFrame(double nowSeconds);

    bool WantsContinuous() const;
    double RemainingAnimation() const;

private:
    double m_animationUntil = 0.0;
    double m_now = 0.0;
    bool m_redrawPending = false;
};

}
