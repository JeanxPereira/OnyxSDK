#include <Onyx/Render/AnimationPlayer.h>
#include <Onyx/Render/Skinning.h>
#include <Onyx/TestKit/SceneFixtures.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

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
