#include <Onyx/Render/Skinning.h>
#include <Onyx/TestKit/SceneFixtures.h>

#include <doctest/doctest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <string>

using namespace Onyx;

namespace {

void ExpectIdentity(const glm::mat4& m) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            CHECK(m[c][r] == doctest::Approx(c == r ? 1.0f : 0.0f).epsilon(1e-5));
        }
    }
}

Domain::MeshPart SkinnedPart(const char* name, size_t vertices) {
    Domain::MeshPart part;
    part.name = name;
    part.positions.assign(vertices, glm::vec3(0.0f));
    part.joints.assign(vertices, glm::u16vec4(0, 1, 0, 0));
    part.weights.assign(vertices, glm::vec4(0.5f, 0.5f, 0.0f, 0.0f));
    part.jointMap = {0, 1};
    return part;
}

}

TEST_CASE("RestPalette is identity when inverseBind inverts the world bind") {
    Domain::Skeleton skeleton;
    Domain::Joint root;
    root.name = "root";
    root.localBind = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));
    root.inverseBind = glm::inverse(root.localBind);
    Domain::Joint child;
    child.name = "child";
    child.parent = 0;
    child.localBind = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 1.5f)), 0.7f,
                                  glm::vec3(1.0f, 0.0f, 0.0f));
    child.inverseBind = glm::inverse(root.localBind * child.localBind);
    skeleton.joints = {root, child};

    const std::vector<glm::mat4> palette = Render::RestPalette(skeleton);
    REQUIRE(palette.size() == 2);
    ExpectIdentity(palette[0]);
    ExpectIdentity(palette[1]);

    const std::vector<glm::mat4> world = Render::RestWorldTransforms(skeleton);
    const std::vector<glm::vec3> positions = Render::JointWorldPositions(skeleton, world);
    REQUIRE(positions.size() == 2);
    CHECK(positions[0].x == doctest::Approx(1.0f));
    CHECK(positions[0].y == doctest::Approx(2.0f));
    CHECK(positions[0].z == doctest::Approx(3.0f));
    CHECK(positions[1] == (glm::vec3(world[1][3])));
}

TEST_CASE("ValidateSkinning accepts the skinned cube") {
    const TestKit::SceneFixture fixture = TestKit::BuildSkinnedCube();
    CHECK(Render::ValidateSkinning(fixture.scene).empty());
}

TEST_CASE("ValidateSkinning reports each malformation on its part") {
    Domain::Scene scene;
    auto skeleton = std::make_shared<Domain::Skeleton>();
    skeleton->joints.resize(2);
    scene.skeleton = skeleton;

    Domain::MeshPart badMap = SkinnedPart("badMap", 3);
    badMap.jointMap = {0, 2};
    Domain::MeshPart badIndex = SkinnedPart("badIndex", 3);
    badIndex.joints[1] = glm::u16vec4(0, 5, 0, 0);
    Domain::MeshPart badStreams = SkinnedPart("badStreams", 3);
    badStreams.weights.pop_back();
    Domain::MeshPart unusedSlot = SkinnedPart("unusedSlot", 3);
    unusedSlot.joints[0] = glm::u16vec4(0, 1, 9, 9);
    scene.parts = {badMap, badIndex, badStreams, unusedSlot};

    const std::vector<Services::Diag> diags = Render::ValidateSkinning(scene);
    REQUIRE(diags.size() == 3);
    CHECK(diags[0].code == "render.skinning.jointMap");
    CHECK(diags[0].message.find("badMap") != std::string::npos);
    CHECK(diags[1].code == "render.skinning.jointIndex");
    CHECK(diags[1].message.find("badIndex") != std::string::npos);
    CHECK(diags[2].code == "render.skinning.streams");
    CHECK(diags[2].message.find("badStreams") != std::string::npos);
    for (const Services::Diag& d : diags) {
        CHECK(d.severity == Services::Severity::Error);
    }
}

TEST_CASE("ValidateSkinning rejects a skinned part in a scene without a skeleton") {
    Domain::Scene scene;
    scene.parts = {SkinnedPart("orphan", 2)};
    const std::vector<Services::Diag> diags = Render::ValidateSkinning(scene);
    REQUIRE(diags.size() == 1);
    CHECK(diags[0].code == "render.skinning.jointMap");
}

TEST_CASE("ValidateSkinning rejects a jointMap with empty vertex streams") {
    Domain::Scene scene;
    auto skeleton = std::make_shared<Domain::Skeleton>();
    skeleton->joints.resize(2);
    scene.skeleton = skeleton;
    Domain::MeshPart part;
    part.name = "mapOnly";
    part.positions.assign(3, glm::vec3(0.0f));
    part.jointMap = {0, 1};
    scene.parts = {part};

    const std::vector<Services::Diag> diags = Render::ValidateSkinning(scene);
    REQUIRE(diags.size() == 1);
    CHECK(diags[0].code == "render.skinning.streams");
    CHECK(diags[0].message.find("mapOnly") != std::string::npos);
}

TEST_CASE("ValidateSkinning reports a joint whose parent does not precede it") {
    Domain::Scene scene;
    auto skeleton = std::make_shared<Domain::Skeleton>();
    skeleton->joints.resize(3);
    skeleton->joints[1].parent = 2;
    skeleton->joints[2].parent = 0;
    scene.skeleton = skeleton;
    const std::vector<Services::Diag> diags = Render::ValidateSkinning(scene);
    REQUIRE(diags.size() == 1);
    CHECK(diags[0].code == "render.skinning.order");
    CHECK(diags[0].severity == Services::Severity::Error);
}
