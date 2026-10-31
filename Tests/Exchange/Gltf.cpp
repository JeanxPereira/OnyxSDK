#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <doctest/doctest.h>

#include <Onyx/Exchange/Gltf.h>
#include <Onyx/TestKit/SceneFixtures.h>

#include "Support.h"

#include <cstddef>
#include <string>

using namespace Onyx;

namespace {

struct Parsed {
    cgltf_data* data = nullptr;
    cgltf_result parse = cgltf_result_io_error;
    cgltf_result buffers = cgltf_result_io_error;
    cgltf_result validate = cgltf_result_io_error;

    explicit Parsed(const std::filesystem::path& path) {
        cgltf_options options{};
        const std::string file = path.string();
        parse = cgltf_parse_file(&options, file.c_str(), &data);
        if (parse != cgltf_result_success) return;
        buffers = cgltf_load_buffers(&options, data, file.c_str());
        validate = cgltf_validate(data);
    }

    ~Parsed() {
        if (data) cgltf_free(data);
    }

    Parsed(const Parsed&) = delete;
    Parsed& operator=(const Parsed&) = delete;

    bool Ok() const {
        return parse == cgltf_result_success && buffers == cgltf_result_success && validate == cgltf_result_success;
    }
};

size_t PopulatedKeyLists(const Domain::AnimClip& clip) {
    size_t n = 0;
    for (const Domain::AnimChannel& c : clip.channels) {
        n += !c.rotation.empty();
        n += !c.translation.empty();
        n += !c.scale.empty();
    }
    return n;
}

}

TEST_CASE("WriteGltf round-trips the animated chain through cgltf") {
    Tests::TempDir dir("gltf_chain");
    const TestKit::SceneFixture fixture = TestKit::BuildAnimatedChain();
    const auto path = dir.path / "chain.glb";

    const Result<void> written = Exchange::WriteGltf(fixture.scene, path);
    REQUIRE(written.ok());

    Parsed parsed(path);
    REQUIRE(parsed.parse == cgltf_result_success);
    CHECK(parsed.buffers == cgltf_result_success);
    CHECK(parsed.validate == cgltf_result_success);
    const cgltf_data& d = *parsed.data;
    CHECK(d.meshes_count == 1);
    REQUIRE(d.skins_count == 1);
    CHECK(d.skins[0].joints_count == fixture.scene.skeleton->joints.size());
    REQUIRE(d.skins[0].inverse_bind_matrices);
    CHECK(d.skins[0].inverse_bind_matrices->count == fixture.scene.skeleton->joints.size());
    REQUIRE(d.animations_count == 1);
    CHECK(d.animations[0].channels_count == PopulatedKeyLists(fixture.scene.animations->clips[0]));
    CHECK(d.animations[0].channels_count > 0);
    CHECK(d.meshes[0].primitives_count == fixture.scene.parts.size());
    for (cgltf_size i = 0; i < d.accessors_count; ++i) {
        CHECK(d.accessors[i].has_min);
        CHECK(d.accessors[i].has_max);
    }
}

TEST_CASE("WriteGltf exports all 200 joints of the joint chain") {
    Tests::TempDir dir("gltf_200");
    const TestKit::SceneFixture fixture = TestKit::BuildJointChain200();
    const auto path = dir.path / "chain200.glb";

    REQUIRE(Exchange::WriteGltf(fixture.scene, path).ok());

    Parsed parsed(path);
    REQUIRE(parsed.Ok());
    REQUIRE(parsed.data->skins_count == 1);
    CHECK(parsed.data->skins[0].joints_count == 200);
    CHECK(parsed.data->skins[0].inverse_bind_matrices->count == 200);
    CHECK(parsed.data->nodes_count == 201);
}

TEST_CASE("includeSkin=false omits the skin") {
    Tests::TempDir dir("gltf_noskin");
    const TestKit::SceneFixture fixture = TestKit::BuildAnimatedChain();
    const auto path = dir.path / "noskin.glb";
    Exchange::GltfOptions options;
    options.includeSkin = false;

    REQUIRE(Exchange::WriteGltf(fixture.scene, path, options).ok());

    Parsed parsed(path);
    REQUIRE(parsed.Ok());
    CHECK(parsed.data->skins_count == 0);
    CHECK(parsed.data->meshes_count == 1);
}

TEST_CASE("includeAnimations=false omits the animation") {
    Tests::TempDir dir("gltf_noanim");
    const TestKit::SceneFixture fixture = TestKit::BuildAnimatedChain();
    const auto path = dir.path / "noanim.glb";
    Exchange::GltfOptions options;
    options.includeAnimations = false;

    REQUIRE(Exchange::WriteGltf(fixture.scene, path, options).ok());

    Parsed parsed(path);
    REQUIRE(parsed.Ok());
    CHECK(parsed.data->animations_count == 0);
    CHECK(parsed.data->skins_count == 1);
}

TEST_CASE("embedBuffers=false writes a .gltf and a sibling .bin") {
    Tests::TempDir dir("gltf_external");
    const TestKit::SceneFixture fixture = TestKit::BuildAnimatedChain();
    const auto path = dir.path / "external.gltf";
    Exchange::GltfOptions options;
    options.embedBuffers = false;

    REQUIRE(Exchange::WriteGltf(fixture.scene, path, options).ok());

    CHECK(std::filesystem::exists(dir.path / "external.bin"));
    CHECK(std::filesystem::file_size(dir.path / "external.bin") > 0);
    Parsed parsed(path);
    REQUIRE(parsed.Ok());
    REQUIRE(parsed.data->buffers_count == 1);
    CHECK(std::string(parsed.data->buffers[0].uri) == "external.bin");
    CHECK(parsed.data->animations_count == 1);
    CHECK(parsed.data->images_count == 1);
}

TEST_CASE("WriteGltf refuses a scene with no parts") {
    Tests::TempDir dir("gltf_empty");
    const Result<void> r = Exchange::WriteGltf(Domain::Scene{}, dir.path / "empty.glb");
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().code == "exchange.gltf.empty");
}

TEST_CASE("the sphere grid diffuse texture lands as an embedded PNG image") {
    Tests::TempDir dir("gltf_textured");
    const TestKit::SceneFixture fixture = TestKit::BuildCorpus()[1];
    REQUIRE(fixture.textured);
    const auto path = dir.path / "grid.glb";

    REQUIRE(Exchange::WriteGltf(fixture.scene, path).ok());

    Parsed parsed(path);
    REQUIRE(parsed.Ok());
    const cgltf_data& d = *parsed.data;
    // Diffuse, Normal and Occlusion are the exported roles; the other seven textures stay out.
    CHECK(d.textures_count == 3);
    REQUIRE(d.images_count == 3);
    for (cgltf_size i = 0; i < d.images_count; ++i) CHECK(std::string(d.images[i].mime_type) == "image/png");
    bool bound = false;
    for (cgltf_size i = 0; i < d.materials_count; ++i) {
        bound = bound || d.materials[i].pbr_metallic_roughness.base_color_texture.texture != nullptr;
    }
    CHECK(bound);
}

TEST_CASE("a block-compressed texture is skipped") {
    Tests::TempDir dir("gltf_bc1");
    TestKit::SceneFixture fixture = TestKit::BuildCorpus()[1];
    REQUIRE_FALSE(fixture.scene.textures.empty());
    for (Domain::Texture& texture : fixture.scene.textures) {
        texture.format = Domain::PixelFormat::Bc1;
        texture.width = 4;
        texture.height = 4;
        texture.pixels.assign(8, std::byte{0});
    }
    const auto path = dir.path / "bc1.glb";

    REQUIRE(Exchange::WriteGltf(fixture.scene, path).ok());

    Parsed parsed(path);
    REQUIRE(parsed.Ok());
    CHECK(parsed.data->images_count == 0);
    for (cgltf_size i = 0; i < parsed.data->materials_count; ++i) {
        CHECK(parsed.data->materials[i].pbr_metallic_roughness.base_color_texture.texture == nullptr);
    }
}
