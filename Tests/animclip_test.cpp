// Pure, no-device tests for the synthetic animated corpus scene (Task 1 of
// the v1.1 animation milestone). Nothing here touches Vulkan: it drives
// AnimationPlayer directly over BuildAnimatedChain()'s hand-built clip, so
// a failure here means the clip or the bake is wrong, never the renderer.
#include <doctest/doctest.h>

#include <Onyx/Rendering/AnimationPlayer.h>
#include <Onyx/Rendering/JointPalette.h>
#include "CorpusScenes.h"

#include <cmath>

namespace {

// Largest absolute difference between two joint-local rotation sets, in the
// Q.14 encoded-degree units vectors5/m_jointLocalRot both carry.
float MaxRotDelta(const std::vector<glm::vec4>& a, const std::vector<glm::vec4>& b) {
    float worst = 0.0f;
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i)
        for (int c = 0; c < 4; ++c)
            worst = std::max(worst, std::fabs(a[i][c] - b[i][c]));
    return worst;
}

// Largest absolute per-element difference between two same-length joint
// palettes (glm::mat4 is 4 glm::vec4 columns -- walk all 16 floats of each).
float MaxMatDelta(const std::vector<glm::mat4>& a, const std::vector<glm::mat4>& b) {
    float worst = 0.0f;
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i)
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                worst = std::max(worst, std::fabs(a[i][col][row] - b[i][col][row]));
    return worst;
}

} // namespace

TEST_CASE("AnimClip: BuildAnimatedChain carries one skinning act with a baked clip") {
    auto cs = Onyx::OracleTool::BuildAnimatedChain();

    // .get() != nullptr, not the shared_ptr comparison directly: MSVC STL's
    // std::operator<<(ostream&, const shared_ptr<T>&) forwards to
    // `os << ptr.get()`, and doctest eagerly instantiates that path to
    // format a would-be failure message even when the check passes -- for
    // this pointee type that forwarded call fails to find a viable
    // basic_ostream overload and turns into a hard compile error. Comparing
    // the raw pointer sidesteps shared_ptr's streaming operator entirely and
    // asserts the identical thing.
    REQUIRE(cs.scene.animations.get() != nullptr);
    CHECK(cs.scene.animations->FindSkinningTypeIndex() == 0);
    REQUIRE(cs.scene.animations->groups.size() == 1);
    REQUIRE(cs.scene.animations->groups[0].acts.size() == 1);
    CHECK(cs.scene.animations->groups[0].acts[0].duration == doctest::Approx(1.0f));
    REQUIRE(cs.scene.skeleton.get() != nullptr);

    Onyx::Rendering::AnimationPlayer player;
    player.SetAnimation(cs.scene.animations.get(), 0, 0, cs.scene.skeleton.get());

    REQUIRE(player.GetBaked() != nullptr);
    CHECK(player.GetFrameCount() == 31);
    CHECK(player.GetCurrentActName() == "bend");
    CHECK(player.GetDuration() == doctest::Approx(1.0f));
}

TEST_CASE("AnimClip: t=0 holds the rest pose and mid-clip moves away from it") {
    auto cs = Onyx::OracleTool::BuildAnimatedChain();

    Onyx::Rendering::AnimationPlayer player;
    player.SetAnimation(cs.scene.animations.get(), 0, 0, cs.scene.skeleton.get());

    // The bake captures frame 0 before walking any stream, so t=0 is the
    // skeleton's own rest pose -- this is what lets the render gate assert
    // "animation set but not advanced changes nothing".
    player.SetTime(0.0f);
    const std::vector<glm::vec4> atZero = player.GetJointRotations();
    REQUIRE(atZero.size() == cs.scene.skeleton->joints.size());
    for (size_t i = 0; i < atZero.size(); ++i)
        CHECK(atZero[i].x == doctest::Approx(float(cs.scene.skeleton->vectors5[i].x)));

    // Guards the fixture itself: joints 1 and 2 must carry a real (nonzero)
    // encoded rest rotation, or the comparison above degrades to 0 == 0 and
    // a Reset() that ignored vectors5 entirely would pass it.
    CHECK(atZero[1].x != 0.0f);
    CHECK(atZero[2].x != 0.0f);

    player.SetTime(0.5f);
    const std::vector<glm::vec4> atMid = player.GetJointRotations();

    // Joint 0 is the unbent root and is never keyed; joints 1 and 2 are.
    CHECK(atMid[0].x == doctest::Approx(atZero[0].x));
    CHECK(MaxRotDelta(atZero, atMid) > 1.0f);
}

TEST_CASE("AnimClip: Stop returns the player to the rest pose") {
    auto cs = Onyx::OracleTool::BuildAnimatedChain();

    Onyx::Rendering::AnimationPlayer player;
    player.SetAnimation(cs.scene.animations.get(), 0, 0, cs.scene.skeleton.get());
    player.SetTime(0.0f);
    const std::vector<glm::vec4> rest = player.GetJointRotations();

    player.SetTime(0.5f);
    REQUIRE(MaxRotDelta(rest, player.GetJointRotations()) > 1.0f);

    player.Stop();
    CHECK(player.IsPlaying() == false);
    CHECK(MaxRotDelta(rest, player.GetJointRotations()) == doctest::Approx(0.0f));
}

// Fix-round Finding 1 (device-less coverage for the Task 3/4 bugfix): the
// VkAnimation oracle gate's (a)-vs-(b) render comparison (0.0000% differing,
// every run) demonstrated "animated pose at t=0 IS the rest pose" -- but
// that gate returns 77 (skip) on any machine with no Vulkan device, and
// nothing in this file (which runs everywhere, no GPU needed) pinned the
// same property at the AnimationPlayer::ComputeJointMatrices() level. This
// is exactly the invariant that AnimationPlayer.cpp's ComputeJointMatrices
// fix (reading joint.bindToJointMat instead of the dead
// matrixes3[joint.invId] path) restores: a fresh player, right after
// SetAnimation() (which bakes and applies frame 0 -- the rest pose, per
// BuildAnimatedChain's own comment: "Sample index 0 is never read by the
// bake... so it is written for completeness only"), must produce the exact
// same joint palette Rendering::ComputeJointPalette() computes directly
// from the skeleton's rest-pose Vectors4/5/6 -- same TRS chain, same
// bindToJointMat, just reached through two different code paths. Before
// the fix this would have failed with a large delta (the animated path
// dropped the inverse-bind correction the rest-pose path applies); a
// regression back to the old matrixes3/invId read would fail this again,
// on any machine, no Vulkan device required.
TEST_CASE("AnimClip: ComputeJointMatrices at t=0 matches ComputeJointPalette's rest pose") {
    auto cs = Onyx::OracleTool::BuildAnimatedChain();

    Onyx::Rendering::AnimationPlayer player;
    // SetAnimation() bakes the clip and applies frame 0 (ApplyBakedAt(0.0f))
    // before returning, leaving the player at t=0 with no further SetTime()
    // needed -- this is deliberately the player's state right after the
    // call, not a state this test has to construct.
    player.SetAnimation(cs.scene.animations.get(), 0, 0, cs.scene.skeleton.get());
    REQUIRE(player.GetTime() == doctest::Approx(0.0f));

    std::vector<glm::mat4> animated = player.ComputeJointMatrices();
    std::vector<glm::mat4> rest = Onyx::Rendering::ComputeJointPalette(*cs.scene.skeleton, nullptr);

    REQUIRE(animated.size() == rest.size());
    REQUIRE(animated.size() == cs.scene.skeleton->joints.size());
    CHECK(MaxMatDelta(animated, rest) == doctest::Approx(0.0f).epsilon(0.0001));
}
