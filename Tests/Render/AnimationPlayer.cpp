#include <Onyx/Render/AnimationPlayer.h>
#include <Onyx/Render/Skinning.h>
#include <Onyx/TestKit/SceneFixtures.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

using namespace Onyx;
using Render::AnimationPlayer;
using Render::LoopMode;

namespace {

float MaxDiff(const glm::mat4& a, const glm::mat4& b) {
    float worst = 0.0f;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            worst = std::max(worst, std::fabs(a[c][r] - b[c][r]));
        }
    }
    return worst;
}

bool SamePalette(std::span<const glm::mat4> a, const std::vector<glm::mat4>& b, float tolerance) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (MaxDiff(a[i], b[i]) > tolerance) {
            return false;
        }
    }
    return true;
}

struct Chain {
    TestKit::SceneFixture fixture = TestKit::BuildAnimatedChain();
    AnimationPlayer player;

    Chain() { player.Set(fixture.scene.skeleton, fixture.scene.animations, 0); }
};

}

TEST_CASE("Palette at t=0 reproduces the rest palette") {
    Chain chain;
    REQUIRE(chain.player.HasClip());
    CHECK(chain.player.Clip() == 0);
    CHECK(chain.player.Duration() == doctest::Approx(1.0f));
    CHECK(chain.player.Palette().size() == 3);
    CHECK(chain.player.WorldTransforms().size() == 3);
    const std::vector<glm::mat4> rest = Render::RestPalette(*chain.fixture.scene.skeleton);
    CHECK(SamePalette(chain.player.Palette(), rest, 1e-5f));
}

TEST_CASE("SetTime mid-clip moves joints 1 and 2 only") {
    Chain chain;
    const std::vector<glm::mat4> rest = Render::RestPalette(*chain.fixture.scene.skeleton);
    chain.player.SetTime(0.5f);
    const std::span<const glm::mat4> palette = chain.player.Palette();
    CHECK(MaxDiff(palette[0], rest[0]) < 1e-5f);
    CHECK(MaxDiff(palette[1], rest[1]) > 1e-2f);
    CHECK(MaxDiff(palette[2], rest[2]) > 1e-2f);
}

TEST_CASE("Advance loops, stops or reverses at the end") {
    SUBCASE("Loop wraps") {
        Chain chain;
        chain.player.SetLoop(LoopMode::Loop);
        chain.player.SetPlaying(true);
        CHECK(chain.player.Advance(1.25f));
        CHECK(chain.player.Time() == doctest::Approx(0.25f).epsilon(1e-4));
        CHECK(chain.player.IsPlaying());
    }
    SUBCASE("None clamps at the end and stops") {
        Chain chain;
        chain.player.SetLoop(LoopMode::None);
        chain.player.SetPlaying(true);
        CHECK(chain.player.Advance(1.25f));
        CHECK(chain.player.Time() == doctest::Approx(1.0f));
        CHECK_FALSE(chain.player.IsPlaying());
        CHECK_FALSE(chain.player.Advance(0.5f));
    }
    SUBCASE("PingPong reflects and comes back") {
        Chain chain;
        chain.player.SetLoop(LoopMode::PingPong);
        chain.player.SetPlaying(true);
        CHECK(chain.player.Advance(1.25f));
        CHECK(chain.player.Time() == doctest::Approx(0.75f).epsilon(1e-4));
        CHECK(chain.player.Advance(0.5f));
        CHECK(chain.player.Time() == doctest::Approx(0.25f).epsilon(1e-4));
        CHECK(chain.player.Advance(0.25f));
        CHECK(chain.player.Time() == doctest::Approx(0.0f).epsilon(1e-4));
        CHECK(chain.player.Advance(0.5f));
        CHECK(chain.player.Time() == doctest::Approx(0.5f).epsilon(1e-4));
    }
}

TEST_CASE("Advance does nothing while paused") {
    Chain chain;
    CHECK_FALSE(chain.player.Advance(0.3f));
    CHECK(chain.player.Time() == 0.0f);
}

TEST_CASE("Speed scales playback") {
    Chain chain;
    chain.player.SetSpeed(2.0f);
    chain.player.SetPlaying(true);
    chain.player.Advance(0.1f);
    CHECK(chain.player.Time() == doctest::Approx(0.2f));
}

TEST_CASE("Stop restores the rest pose") {
    Chain chain;
    chain.player.SetPlaying(true);
    chain.player.SetTime(0.5f);
    chain.player.Stop();
    CHECK_FALSE(chain.player.IsPlaying());
    CHECK(chain.player.Time() == 0.0f);
    const std::vector<glm::mat4> rest = Render::RestPalette(*chain.fixture.scene.skeleton);
    CHECK(SamePalette(chain.player.Palette(), rest, 1e-5f));
}

TEST_CASE("A clip index past the set leaves no clip") {
    TestKit::SceneFixture fixture = TestKit::BuildAnimatedChain();
    AnimationPlayer player;
    player.Set(fixture.scene.skeleton, fixture.scene.animations, 7);
    CHECK_FALSE(player.HasClip());
    CHECK(player.Duration() == 0.0f);
    CHECK_FALSE(player.Advance(0.1f));
    const std::vector<glm::mat4> rest = Render::RestPalette(*fixture.scene.skeleton);
    CHECK(SamePalette(player.Palette(), rest, 1e-5f));
}

TEST_CASE("Nine distinct clips evict the least recently used bake") {
    TestKit::SceneFixture fixture = TestKit::BuildAnimatedChain();
    auto set = std::make_shared<Domain::AnimationSet>();
    for (int i = 0; i < 9; ++i) {
        set->clips.push_back(fixture.scene.animations->clips[0]);
    }
    AnimationPlayer player;
    for (uint32_t clip = 0; clip < 8; ++clip) {
        player.Set(fixture.scene.skeleton, set, clip);
    }
    CHECK(player.BakedClips() == 8);
    CHECK(player.BakeCount() == 8);

    player.Set(fixture.scene.skeleton, set, 0);
    CHECK(player.BakeCount() == 8);

    player.Set(fixture.scene.skeleton, set, 8);
    CHECK(player.BakedClips() == 8);
    CHECK(player.BakeCount() == 9);

    player.Set(fixture.scene.skeleton, set, 1);
    CHECK(player.BakeCount() == 10);
    player.Set(fixture.scene.skeleton, set, 0);
    CHECK(player.BakeCount() == 10);
    CHECK(player.BakedClips() == 8);
}

TEST_CASE("Additive clips add to the bind pose") {
    TestKit::SceneFixture fixture = TestKit::BuildAnimatedChain();
    auto set = std::make_shared<Domain::AnimationSet>();
    Domain::AnimClip clip;
    clip.name = "lift";
    clip.duration = 1.0f;
    clip.additive = true;
    Domain::AnimChannel channel;
    channel.joint = 0;
    channel.translation = {{0.0f, glm::vec3(0.0f)}, {1.0f, glm::vec3(0.0f, 2.0f, 0.0f)}};
    clip.channels.push_back(channel);
    set->clips.push_back(clip);

    AnimationPlayer player;
    player.Set(fixture.scene.skeleton, set, 0);
    const std::vector<glm::mat4> rest = Render::RestWorldTransforms(*fixture.scene.skeleton);
    player.SetTime(1.0f);
    CHECK(player.WorldTransforms()[0][3].y == doctest::Approx(rest[0][3].y + 2.0f));
    player.SetTime(0.5f);
    CHECK(player.WorldTransforms()[0][3].y == doctest::Approx(rest[0][3].y + 1.0f).epsilon(1e-3));
}

TEST_CASE("Off-frame times interpolate between baked frames") {
    Chain chain;
    const Domain::Skeleton& skeleton = *chain.fixture.scene.skeleton;
    const Domain::AnimChannel& channel = chain.fixture.scene.animations->clips[0].channels[0];
    REQUIRE(channel.joint == 1);

    // 0.51 s lies between the 0.5 s key and the 1 s key, at fraction 0.02.
    const glm::quat expected = glm::slerp(channel.rotation[1].value, channel.rotation[2].value, 0.02f);
    glm::mat4 local = glm::mat4_cast(expected);
    local[3] = skeleton.joints[1].localBind[3];
    const std::vector<glm::mat4> rest = Render::RestWorldTransforms(skeleton);
    const glm::mat4 world = rest[0] * local;

    chain.player.SetTime(0.51f);
    CHECK(MaxDiff(chain.player.WorldTransforms()[1], world) < 1e-4f);
}

TEST_CASE("A translation-only channel keeps the bind rotation and interpolates") {
    auto skeleton = std::make_shared<Domain::Skeleton>();
    skeleton->joints.resize(2);
    skeleton->joints[1].parent = 0;
    skeleton->joints[1].localBind = glm::rotate(glm::mat4(1.0f), 0.8f, glm::vec3(0.0f, 1.0f, 0.0f));
    skeleton->joints[1].inverseBind = glm::inverse(skeleton->joints[1].localBind);

    auto set = std::make_shared<Domain::AnimationSet>();
    Domain::AnimClip clip;
    clip.duration = 1.0f;
    Domain::AnimChannel channel;
    channel.joint = 1;
    channel.translation = {{0.0f, glm::vec3(0.0f)}, {1.0f, glm::vec3(2.0f, 0.0f, 0.0f)}};
    clip.channels.push_back(channel);
    set->clips.push_back(clip);

    AnimationPlayer player;
    player.Set(skeleton, set, 0);
    player.SetTime(0.25f);
    const glm::mat4 world = player.WorldTransforms()[1];
    CHECK(world[3].x == doctest::Approx(0.5f).epsilon(1e-5));
    CHECK(world[3].y == doctest::Approx(0.0f).scale(1.0).epsilon(1e-5));
    CHECK(world[3].z == doctest::Approx(0.0f).scale(1.0).epsilon(1e-5));
    glm::mat4 bindRotation = skeleton->joints[1].localBind;
    bindRotation[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    glm::mat4 gotRotation = world;
    gotRotation[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(MaxDiff(gotRotation, bindRotation) < 1e-5f);
}
