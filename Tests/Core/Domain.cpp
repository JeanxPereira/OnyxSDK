#include <doctest/doctest.h>

#include <Onyx/Core/Domain/Animation.h>
#include <Onyx/Core/Domain/Audio.h>
#include <Onyx/Core/Domain/BoundingBox.h>
#include <Onyx/Core/Domain/AssetEntry.h>
#include <Onyx/Core/Domain/Material.h>
#include <Onyx/Core/Domain/Mesh.h>
#include <Onyx/Core/Domain/Scene.h>
#include <Onyx/Core/Domain/Skeleton.h>
#include <Onyx/Core/Domain/Table.h>
#include <Onyx/Core/Domain/Text.h>
#include <Onyx/Core/Domain/Texture.h>

#include <glm/gtc/matrix_transform.hpp>

#include <vector>

using namespace Onyx::Domain;

TEST_CASE("Domain.TextureIsValidRgba8") {
    Texture t;
    t.width = 2;
    t.height = 2;
    t.format = PixelFormat::Rgba8;
    t.pixels.resize(16);
    CHECK(t.IsValid());
    t.pixels.resize(15);
    CHECK_FALSE(t.IsValid());
}

TEST_CASE("Domain.TextureIsValidBc1RoundsToBlocks") {
    Texture t;
    t.width = 5;
    t.height = 5;
    t.format = PixelFormat::Bc1;
    CHECK(ExpectedByteSize(PixelFormat::Bc1, 5, 5) == 32);
    t.pixels.resize(32);
    CHECK(t.IsValid());
    t.pixels.resize(8);
    CHECK_FALSE(t.IsValid());
    CHECK(ExpectedByteSize(PixelFormat::Bc7, 4, 4) == 16);
    CHECK(ExpectedByteSize(PixelFormat::RgbaF16, 2, 2) == 32);
    CHECK(IsBlockCompressed(PixelFormat::Bc3));
    CHECK_FALSE(IsBlockCompressed(PixelFormat::Rgba8));
}

TEST_CASE("Domain.BoundingBoxOfPoints") {
    const std::vector<glm::vec3> pts{{-1, 2, 3}, {4, -5, 6}, {0, 0, 0}};
    const BoundingBox box = BoundingBox::Of(pts);
    CHECK(box.min == glm::vec3(-1, -5, 0));
    CHECK(box.max == glm::vec3(4, 2, 6));
    CHECK(box.Center() == glm::vec3(1.5f, -1.5f, 3));
    CHECK(box.Radius() > 0.0f);
    const BoundingBox empty = BoundingBox::Of({});
    CHECK(empty.min == glm::vec3(0));
    CHECK(empty.max == glm::vec3(0));
}

TEST_CASE("Domain.SceneComputeBoundsAppliesTransform") {
    Scene s;
    s.transform = glm::translate(glm::mat4(1), glm::vec3(10, 0, 0));
    MeshPart p;
    p.positions = {{0, 0, 0}, {1, 1, 1}};
    s.parts.push_back(p);
    const BoundingBox b = s.ComputeBounds();
    CHECK(b.min.x == doctest::Approx(10.0f));
    CHECK(b.max.x == doctest::Approx(11.0f));
    CHECK(Scene{}.ComputeBounds().max == glm::vec3(0));
}

TEST_CASE("Domain.PartFlagsCompose") {
    const PartFlags f = PartFlags::Sky | PartFlags::Hidden;
    CHECK(HasFlag(f, PartFlags::Sky));
    CHECK(HasFlag(f, PartFlags::Hidden));
    CHECK_FALSE(HasFlag(PartFlags::Sky, PartFlags::Hidden));
    CHECK((f & PartFlags::Hidden) == PartFlags::Hidden);
    CHECK((PartFlags::Sky & PartFlags::Hidden) == PartFlags::None);
}

TEST_CASE("Domain.AssetEntryDefaults") {
    AssetEntry e;
    CHECK(e.source.Empty());
    CHECK(e.flags == NodeFlags::None);
    CHECK(HasFlag(NodeFlags::Failed | NodeFlags::None, NodeFlags::Failed));
    e.source = {0, 16, 4};
    CHECK_FALSE(e.source.Empty());
}
