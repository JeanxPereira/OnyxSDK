#include "Onyx/Ui/FrameClock.h"

#include <algorithm>

namespace Onyx::Ui {

void FrameClock::RequestRedraw() { m_redrawPending = true; }

void FrameClock::RequestAnimation(double seconds) {
    if (seconds <= 0.0) {
        RequestRedraw();
        return;
    }
    m_animationUntil = std::max(m_animationUntil, m_now + seconds);
}

bool FrameClock::WantsContinuous() const { return m_redrawPending || m_animationUntil > m_now; }

double FrameClock::RemainingAnimation() const {
    return m_animationUntil > m_now ? m_animationUntil - m_now : 0.0;
}

bool FrameClock::BeginFrame(double nowSeconds) {
    m_now = nowSeconds;
    const bool wants = WantsContinuous();
    m_redrawPending = false;
    return wants;
}

}
