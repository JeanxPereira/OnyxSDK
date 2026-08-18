// ── Frame scheduler tests (doctest) ───────────────────────────────────────
//
// Pure timing logic, no window. The cases below are the properties the window
// loop depends on: a request must buy exactly the frames it asked for, and a
// client that stops asking must not leave the loop spinning.

#include <doctest/doctest.h>
#include <Onyx/Services/FrameScheduler.h>

using namespace Onyx::Frame;

TEST_CASE("FrameScheduler idles when nothing asks") {
    Reset();
    CHECK_FALSE(WantsContinuous());
    CHECK_FALSE(BeginFrame(0.0));
    CHECK_FALSE(BeginFrame(10.0));
}

TEST_CASE("FrameScheduler grants a single-frame redraw exactly once") {
    Reset();
    BeginFrame(0.0);

    RequestRedraw();
    CHECK(WantsContinuous());

    // The frame that the request bought.
    CHECK(BeginFrame(0.016));
    // ...and no second one: consumption happens in the step that acts on it,
    // so a stale flag can neither be eaten early nor linger.
    CHECK_FALSE(BeginFrame(0.032));
}

TEST_CASE("FrameScheduler holds frames for the length of an animation") {
    Reset();
    BeginFrame(100.0);          // the clock need not start at zero

    RequestAnimation(0.25);
    CHECK(RemainingAnimation() == doctest::Approx(0.25));

    CHECK(BeginFrame(100.10));
    CHECK(BeginFrame(100.24));
    CHECK(RemainingAnimation() == doctest::Approx(0.01));

    // Expires on its own -- a client that dies mid-animation cannot pin the
    // loop at full speed.
    CHECK_FALSE(BeginFrame(100.26));
    CHECK(RemainingAnimation() == 0.0);
}

TEST_CASE("FrameScheduler extends rather than stacks repeated requests") {
    Reset();
    BeginFrame(0.0);

    // The pattern a transition uses: re-ask every frame with the time left.
    RequestAnimation(0.25);
    BeginFrame(0.10);
    RequestAnimation(0.15);
    CHECK(RemainingAnimation() == doctest::Approx(0.15));

    // A shorter request cannot cut an existing longer one short.
    BeginFrame(0.11);
    RequestAnimation(0.01);
    CHECK(RemainingAnimation() == doctest::Approx(0.14));

    // A longer one does extend it.
    RequestAnimation(1.0);
    CHECK(RemainingAnimation() == doctest::Approx(1.0));
}

TEST_CASE("FrameScheduler treats a zero-length animation as one redraw") {
    Reset();
    BeginFrame(0.0);

    RequestAnimation(0.0);
    CHECK(BeginFrame(0.016));
    CHECK_FALSE(BeginFrame(0.032));

    RequestAnimation(-5.0);
    CHECK(BeginFrame(0.048));
    CHECK_FALSE(BeginFrame(0.064));
}

TEST_CASE("FrameScheduler::Reset drops everything") {
    Reset();
    BeginFrame(0.0);
    RequestAnimation(10.0);
    RequestRedraw();
    CHECK(WantsContinuous());

    Reset();
    CHECK_FALSE(WantsContinuous());
    CHECK(RemainingAnimation() == 0.0);
}
