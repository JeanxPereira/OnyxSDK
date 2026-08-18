#include <Onyx/Services/FrameScheduler.h>

#include <algorithm>

namespace Onyx::Frame {

namespace {

// An absolute deadline rather than a countdown: a client asking every frame
// keeps pushing the same deadline forward instead of accumulating time, and one
// that stops asking simply lets it pass.
double s_animationUntil = 0.0;
double s_now            = 0.0;
bool   s_redrawPending  = false;

} // namespace

void RequestRedraw() { s_redrawPending = true; }

void RequestAnimation(double seconds) {
    if (seconds <= 0.0) {
        RequestRedraw();
        return;
    }
    s_animationUntil = std::max(s_animationUntil, s_now + seconds);
}

bool WantsContinuous() { return s_redrawPending || s_animationUntil > s_now; }

double RemainingAnimation() {
    return (s_animationUntil > s_now) ? (s_animationUntil - s_now) : 0.0;
}

bool BeginFrame(double nowSeconds) {
    s_now = nowSeconds;

    const bool wants = WantsContinuous();
    s_redrawPending  = false;   // single-shot: consumed by the frame it buys
    return wants;
}

void Reset() {
    s_animationUntil = 0.0;
    s_redrawPending  = false;
}

} // namespace Onyx::Frame
