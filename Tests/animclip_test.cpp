// Pure, no-device tests for the synthetic animated corpus scene (Task 1 of
// the v1.1 animation milestone). Nothing here touches Vulkan: it drives
// AnimationPlayer directly over BuildAnimatedChain()'s hand-built clip, so
// a failure here means the clip or the bake is wrong, never the renderer.
#include <doctest/doctest.h>

#include <Onyx/Rendering/AnimationPlayer.h>
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
