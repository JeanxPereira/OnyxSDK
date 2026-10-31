#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <doctest/doctest.h>

#include <Onyx/Exchange/Gltf.h>
#include <Onyx/TestKit/SceneFixtures.h>

#include "Support.h"

#include <cstddef>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

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
        const bool integral = d.accessors[i].component_type == cgltf_component_type_r_32u ||
                              d.accessors[i].component_type == cgltf_component_type_r_16u;
        CHECK(static_cast<bool>(d.accessors[i].has_min) == !integral);
        CHECK(static_cast<bool>(d.accessors[i].has_max) == !integral);
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

namespace {

// A triangle skinned to a two-joint chain: vertex v uses local joint indices `locals[v]` through `jointMap`
// with weights (0.5, 0.5, 0, 0), and the scene carries `clip` as its one animation.
Domain::Scene SkinnedTriangle(std::vector<uint16_t> jointMap, std::vector<glm::u16vec4> locals, Domain::AnimClip clip) {
    auto skeleton = std::make_shared<Domain::Skeleton>();
    skeleton->joints.push_back(Domain::Joint{"root", -1});
    skeleton->joints.push_back(Domain::Joint{"tip", 0});
    auto animations = std::make_shared<Domain::AnimationSet>();
    animations->clips.push_back(std::move(clip));

    Domain::MeshPart part;
    part.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    part.indices = {0, 1, 2};
    part.joints = std::move(locals);
    part.weights.assign(3, glm::vec4(0.5f, 0.5f, 0.0f, 0.0f));
    part.jointMap = std::move(jointMap);

    Domain::Scene scene;
    scene.skeleton = skeleton;
    scene.animations = animations;
    scene.parts.push_back(std::move(part));
    return scene;
}

Domain::AnimClip OneKeyClip() {
    Domain::AnimClip clip;
    clip.name = "clip";
    Domain::AnimChannel channel;
    channel.joint = 1;
    channel.translation = {{0.0f, glm::vec3(0.0f)}, {1.0f, glm::vec3(1.0f)}};
    clip.channels.push_back(channel);
    return clip;
}

const cgltf_accessor* Attribute(const cgltf_data& d, cgltf_attribute_type type) {
    const cgltf_primitive& p = d.meshes[0].primitives[0];
    for (cgltf_size i = 0; i < p.attributes_count; ++i) {
        if (p.attributes[i].type == type) return p.attributes[i].data;
    }
    return nullptr;
}

}

TEST_CASE("WriteGltf writes rotation keys as unit quaternions and a zero quaternion as the identity") {
    Tests::TempDir dir("gltf_quat");
    Domain::AnimClip clip;
    clip.name = "spin";
    Domain::AnimChannel channel;
    channel.joint = 1;
    channel.rotation = {{0.0f, glm::quat(0.0f, 0.0f, 0.0f, 0.0f)}, {1.0f, glm::quat(0.0f, 0.0f, 0.0f, 2.0f)}};
    clip.channels.push_back(channel);
    const auto path = dir.path / "quat.glb";
    REQUIRE(Exchange::WriteGltf(SkinnedTriangle({0, 1}, {{0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}}, clip), path).ok());

    Parsed parsed(path);
    REQUIRE(parsed.Ok());
    REQUIRE(parsed.data->animations_count == 1);
    REQUIRE(parsed.data->animations[0].samplers_count == 1);
    const cgltf_accessor* output = parsed.data->animations[0].samplers[0].output;
    REQUIRE(output->count == 2);
    float first[4] = {};
    float second[4] = {};
    REQUIRE(cgltf_accessor_read_float(output, 0, first, 4));
    REQUIRE(cgltf_accessor_read_float(output, 1, second, 4));
    CHECK(first[0] == 0.0f);
    CHECK(first[1] == 0.0f);
    CHECK(first[2] == 0.0f);
    CHECK(first[3] == 1.0f);
    CHECK(second[2] == doctest::Approx(1.0f));
    CHECK(second[3] == doctest::Approx(0.0f));
}

TEST_CASE("WriteGltf writes an unmapped or out-of-range joint as joint 0 with weight 0") {
    Tests::TempDir dir("gltf_jointmap");
    // Local 0 maps to joint 1, local 1 maps past the two-joint skeleton, local 5 has no mapping.
    const auto path = dir.path / "joints.glb";
    REQUIRE(Exchange::WriteGltf(SkinnedTriangle({1, 9}, {{0, 1, 0, 0}, {0, 5, 0, 0}, {0, 0, 0, 0}}, OneKeyClip()),
                                path)
                .ok());

    Parsed parsed(path);
    REQUIRE(parsed.Ok());
    const cgltf_accessor* joints = Attribute(*parsed.data, cgltf_attribute_type_joints);
    const cgltf_accessor* weights = Attribute(*parsed.data, cgltf_attribute_type_weights);
    REQUIRE(joints);
    REQUIRE(weights);
    for (cgltf_size v = 0; v < 3; ++v) {
        cgltf_uint j[4] = {};
        float w[4] = {};
        REQUIRE(cgltf_accessor_read_uint(joints, v, j, 4));
        REQUIRE(cgltf_accessor_read_float(weights, v, w, 4));
        for (int c = 0; c < 4; ++c) CHECK(j[c] < parsed.data->skins[0].joints_count);
        CHECK(j[0] == 1);
        CHECK(w[0] == 0.5f);
        if (v < 2) {
            CHECK(j[1] == 0);
            CHECK(w[1] == 0.0f);
        }
    }
}

TEST_CASE("WriteGltf keeps the first of two channels on the same joint and path") {
    Tests::TempDir dir("gltf_duplicate");
    Domain::AnimClip clip = OneKeyClip();
    Domain::AnimChannel again;
    again.joint = 1;
    again.translation = {{0.0f, glm::vec3(2.0f)}, {0.5f, glm::vec3(3.0f)}, {1.0f, glm::vec3(4.0f)}};
    again.scale = {{0.0f, glm::vec3(1.0f)}};
    clip.channels.push_back(again);
    const auto path = dir.path / "duplicate.glb";
    REQUIRE(Exchange::WriteGltf(SkinnedTriangle({0, 1}, {{0, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}}, clip), path).ok());

    Parsed parsed(path);
    REQUIRE(parsed.Ok());
    REQUIRE(parsed.data->animations_count == 1);
    const cgltf_animation& animation = parsed.data->animations[0];
    REQUIRE(animation.channels_count == 2);
    for (cgltf_size a = 0; a < animation.channels_count; ++a) {
        for (cgltf_size b = a + 1; b < animation.channels_count; ++b) {
            CHECK_FALSE((animation.channels[a].target_node == animation.channels[b].target_node &&
                         animation.channels[a].target_path == animation.channels[b].target_path));
        }
    }
    CHECK(animation.channels[0].target_path == cgltf_animation_path_type_translation);
    CHECK(animation.channels[0].sampler->input->count == 2);
    CHECK(animation.channels[1].target_path == cgltf_animation_path_type_scale);
}

TEST_CASE("index and joint accessors carry no min or max") {
    Tests::TempDir dir("gltf_bounds");
    const TestKit::SceneFixture fixture = TestKit::BuildAnimatedChain();
    const auto path = dir.path / "bounds.glb";
    REQUIRE(Exchange::WriteGltf(fixture.scene, path).ok());

    std::ifstream in(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    cgltf_options options{};
    cgltf_data* data = nullptr;
    REQUIRE(cgltf_parse(&options, bytes.data(), bytes.size(), &data) == cgltf_result_success);
    bool sawIndex = false;
    bool sawJoints = false;
    bool sawPosition = false;
    for (cgltf_size m = 0; m < data->meshes_count; ++m) {
        for (cgltf_size p = 0; p < data->meshes[m].primitives_count; ++p) {
            const cgltf_primitive& primitive = data->meshes[m].primitives[p];
            if (primitive.indices) {
                sawIndex = true;
                CHECK_FALSE(primitive.indices->has_min);
                CHECK_FALSE(primitive.indices->has_max);
            }
            for (cgltf_size a = 0; a < primitive.attributes_count; ++a) {
                const cgltf_attribute& attribute = primitive.attributes[a];
                if (attribute.type == cgltf_attribute_type_joints) {
                    sawJoints = true;
                    CHECK_FALSE(attribute.data->has_min);
                    CHECK_FALSE(attribute.data->has_max);
                } else if (attribute.type == cgltf_attribute_type_position) {
                    sawPosition = true;
                    CHECK(attribute.data->has_min);
                    CHECK(attribute.data->has_max);
                }
            }
        }
    }
    cgltf_free(data);
    CHECK(sawIndex);
    CHECK(sawJoints);
    CHECK(sawPosition);
}

TEST_CASE("a .gltf whose directory is a file leaves no .bin behind") {
    Tests::TempDir dir("gltf_blocked");
    const auto blocker = dir.path / "blocker";
    { std::ofstream(blocker) << "x"; }
    const TestKit::SceneFixture fixture = TestKit::BuildAnimatedChain();
    Exchange::GltfOptions options;
    options.embedBuffers = false;

    const Result<void> written = Exchange::WriteGltf(fixture.scene, blocker / "x.gltf", options);
    REQUIRE_FALSE(written.ok());
    CHECK(written.error().code == "exchange.gltf.write");
    CHECK_FALSE(std::filesystem::exists(blocker / "x.bin"));
    CHECK_FALSE(std::filesystem::exists(dir.path / "x.bin"));
}

TEST_CASE("a .gltf under a non-ASCII directory is written") {
    Tests::TempDir dir("gltf_unicode");
    const auto folder = dir.path / std::filesystem::path(u8"café");
    const auto path = folder / "x.gltf";
    const TestKit::SceneFixture fixture = TestKit::BuildAnimatedChain();
    Exchange::GltfOptions options;
    options.embedBuffers = false;

    REQUIRE(Exchange::WriteGltf(fixture.scene, path, options).ok());
    CHECK(std::filesystem::exists(folder / "x.bin"));

    std::ifstream in(path, std::ios::binary);
    REQUIRE(in);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    cgltf_options parseOptions{};
    cgltf_data* data = nullptr;
    REQUIRE(cgltf_parse(&parseOptions, bytes.data(), bytes.size(), &data) == cgltf_result_success);
    CHECK(data->meshes_count > 0);
    cgltf_free(data);
}
