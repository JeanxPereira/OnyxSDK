#include <doctest/doctest.h>

#include <Onyx/Ui/FrameClock.h>

using namespace Onyx::Ui;

TEST_CASE("FrameClock is idle until asked") {
    FrameClock clock;
    CHECK_FALSE(clock.WantsContinuous());
    CHECK_FALSE(clock.BeginFrame(1.0));
    CHECK(clock.RemainingAnimation() == 0.0);
}

TEST_CASE("A redraw request buys exactly one frame") {
    FrameClock clock;
    clock.RequestRedraw();
    CHECK(clock.WantsContinuous());
    CHECK(clock.BeginFrame(1.0));
    CHECK_FALSE(clock.BeginFrame(1.1));
}

TEST_CASE("FrameClock is continuous while an animation remains") {
    FrameClock clock;
    clock.BeginFrame(10.0);
    clock.RequestAnimation(0.5);
    CHECK(clock.WantsContinuous());
    CHECK(clock.BeginFrame(10.2));
    CHECK(clock.RemainingAnimation() == doctest::Approx(0.3));
    CHECK(clock.BeginFrame(10.4));
    CHECK_FALSE(clock.BeginFrame(10.5));
    CHECK(clock.RemainingAnimation() == 0.0);
}

TEST_CASE("Repeated animation requests extend the deadline instead of adding up") {
    FrameClock clock;
    clock.BeginFrame(0.0);
    clock.RequestAnimation(1.0);
    clock.BeginFrame(0.5);
    clock.RequestAnimation(1.0);
    CHECK(clock.RemainingAnimation() == doctest::Approx(1.0));
    clock.RequestAnimation(0.1);
    CHECK(clock.RemainingAnimation() == doctest::Approx(1.0));
}

TEST_CASE("A non-positive animation is a plain redraw") {
    FrameClock clock;
    clock.RequestAnimation(0.0);
    CHECK(clock.BeginFrame(1.0));
    CHECK_FALSE(clock.WantsContinuous());
}
