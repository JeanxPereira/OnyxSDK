// ── Onyx::Exchange::ExportSceneData tests (doctest) ────────────────────────
//
// The machine check spec §9 asks for: export the M0 corpus's skinned-cube
// SceneData (Tools/OnyxOracle/CorpusScenes.h -- linked into onyx_tests the
// same way oracle_corpus_test.cpp already does, see Tests/CMakeLists.txt),
// then parse the result back through cgltf (an oracle we did not write)
// and assert the shape a human opening it in Blender would also expect:
// one mesh, one skin, three joints, the same vertex count the source
// SceneData had, one inverse bind matrix per joint, and min/max present
// on every accessor. This file owns the ONE CGLTF_IMPLEMENTATION in the
// whole binary (cgltf's read side; Source/Exchange/GltfExport.cpp only
// ever defines CGLTF_WRITE_IMPLEMENTATION, the write side -- see that
// file's own top comment) -- "have exactly one source file where you
// define CGLTF_IMPLEMENTATION", per cgltf.h's own building instructions.

#include <doctest/doctest.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <Onyx/Exchange/GltfExport.h>

#include <CorpusScenes.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace Onyx::OracleTool;
using Onyx::Exchange::ExportSceneData;
using Onyx::Exchange::GltfOptions;
using Onyx::Parsers::SceneData;

namespace {

std::filesystem::path TempPath(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

// Parses `path` through cgltf, REQUIREs success, and returns the owning
// data pointer (cgltf_free is the caller's job -- doctest REQUIRE aborts
// the test on failure, so callers keep this to one Parse+free pair each
// rather than a shared fixture with cleanup ordering to reason about).
cgltf_data* ParseOrFail(const std::filesystem::path& path) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    cgltf_result res = cgltf_parse_file(&options, path.string().c_str(), &data);
    REQUIRE(res == cgltf_result_success);
    REQUIRE(data != nullptr);
    return data;
}

} // namespace

TEST_CASE("GltfExport: skinned-cube round-trips through cgltf with 1 mesh, 1 skin, 3 joints") {
    CorpusScene cs = BuildSkinnedCube();
    auto path = TempPath("onyx_gltf_skinned_cube.glb");
    std::filesystem::remove(path);

    std::string err;
    REQUIRE(ExportSceneData(cs.scene, path, GltfOptions{}, err));
    CHECK(err.empty());
    REQUIRE(std::filesystem::exists(path));

    cgltf_data* data = ParseOrFail(path);
    CHECK(cgltf_validate(data) == cgltf_result_success);

    CHECK(data->meshes_count == 1);
    REQUIRE(data->meshes_count == 1);
    CHECK(data->meshes[0].primitives_count == cs.scene.meshParts.size());

    CHECK(data->skins_count == 1);
    REQUIRE(data->skins_count == 1);
    CHECK(data->skins[0].joints_count == 3);
    CHECK(data->skins[0].joints_count == cs.scene.skeleton->joints.size());

    cgltf_free(data);
    std::filesystem::remove(path);
}

TEST_CASE("GltfExport: inverse bind matrix accessor count matches joint count") {
    CorpusScene cs = BuildSkinnedCube();
    auto path = TempPath("onyx_gltf_ibm_count.glb");
    std::filesystem::remove(path);

    std::string err;
    REQUIRE(ExportSceneData(cs.scene, path, GltfOptions{}, err));

    cgltf_data* data = ParseOrFail(path);
    REQUIRE(data->skins_count == 1);
    REQUIRE(data->skins[0].inverse_bind_matrices != nullptr);
    CHECK(data->skins[0].inverse_bind_matrices->count == data->skins[0].joints_count);
    CHECK(data->skins[0].inverse_bind_matrices->type == cgltf_type_mat4);

    cgltf_free(data);
    std::filesystem::remove(path);
}

TEST_CASE("GltfExport: every accessor has min and max") {
    CorpusScene cs = BuildSkinnedCube();
    auto path = TempPath("onyx_gltf_minmax.glb");
    std::filesystem::remove(path);

    std::string err;
    REQUIRE(ExportSceneData(cs.scene, path, GltfOptions{}, err));

    cgltf_data* data = ParseOrFail(path);
    REQUIRE(data->accessors_count > 0);
    for (cgltf_size i = 0; i < data->accessors_count; ++i) {
        CAPTURE(i);
        CHECK(data->accessors[i].has_min);
        CHECK(data->accessors[i].has_max);
    }

    cgltf_free(data);
    std::filesystem::remove(path);
}

TEST_CASE("GltfExport: exported vertex count matches the source SceneData") {
    CorpusScene cs = BuildSkinnedCube();
    auto path = TempPath("onyx_gltf_vertex_count.glb");
    std::filesystem::remove(path);

    REQUIRE(cs.scene.meshParts.size() == 1);
    const size_t expectedVerts = cs.scene.meshParts[0].vertices.size();

    std::string err;
    REQUIRE(ExportSceneData(cs.scene, path, GltfOptions{}, err));

    cgltf_data* data = ParseOrFail(path);
    REQUIRE(data->meshes_count == 1);
    REQUIRE(data->meshes[0].primitives_count == 1);

    const cgltf_primitive& prim = data->meshes[0].primitives[0];
    const cgltf_accessor* posAcc = nullptr;
    for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
        if (prim.attributes[i].type == cgltf_attribute_type_position) posAcc = prim.attributes[i].data;
    }
    REQUIRE(posAcc != nullptr);
    CHECK(posAcc->count == expectedVerts);

    cgltf_free(data);
    std::filesystem::remove(path);
}

TEST_CASE("GltfExport: includeSkin=false omits the skin entirely") {
    CorpusScene cs = BuildSkinnedCube();
    auto path = TempPath("onyx_gltf_noskin.glb");
    std::filesystem::remove(path);

    GltfOptions opts;
    opts.includeSkin = false;
    std::string err;
    REQUIRE(ExportSceneData(cs.scene, path, opts, err));

    cgltf_data* data = ParseOrFail(path);
    CHECK(cgltf_validate(data) == cgltf_result_success);
    CHECK(data->skins_count == 0);
    REQUIRE(data->meshes_count == 1);
    for (cgltf_size i = 0; i < data->meshes[0].primitives_count; ++i) {
        const cgltf_primitive& prim = data->meshes[0].primitives[i];
        for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
            CHECK(prim.attributes[a].type != cgltf_attribute_type_joints);
            CHECK(prim.attributes[a].type != cgltf_attribute_type_weights);
        }
    }

    cgltf_free(data);
    std::filesystem::remove(path);
}

TEST_CASE("GltfExport: embedBuffers=false writes a plain .gltf plus a sibling .bin") {
    CorpusScene cs = BuildSkinnedCube();
    auto path = TempPath("onyx_gltf_external.gltf");
    auto binPath = TempPath("onyx_gltf_external.bin");
    std::filesystem::remove(path);
    std::filesystem::remove(binPath);

    GltfOptions opts;
    opts.embedBuffers = false;
    std::string err;
    REQUIRE(ExportSceneData(cs.scene, path, opts, err));
    CHECK(std::filesystem::exists(path));
    CHECK(std::filesystem::exists(binPath));
    REQUIRE(std::filesystem::file_size(binPath) > 0);

    cgltf_data* data = ParseOrFail(path);
    CHECK(data->file_type == cgltf_file_type_gltf);
    CHECK(data->meshes_count == 1);
    CHECK(data->skins_count == 1);
    REQUIRE(data->buffers_count == 1);
    CHECK(std::string(data->buffers[0].uri) == "onyx_gltf_external.bin");

    cgltf_free(data);
    std::filesystem::remove(path);
    std::filesystem::remove(binPath);
}

TEST_CASE("GltfExport: refuses a SceneData with no mesh parts") {
    SceneData empty;
    auto path = TempPath("onyx_gltf_empty.glb");
    std::filesystem::remove(path);

    std::string err;
    CHECK_FALSE(ExportSceneData(empty, path, GltfOptions{}, err));
    CHECK_FALSE(err.empty());
    CHECK_FALSE(std::filesystem::exists(path));
}

// ── Step 5: the external oracle (human gate) ────────────────────────────
// Regenerates the two files docs/gltf-validation.md tells a human to open
// in Blender, at the fixed path that doc names: build/gltf-check/. This is
// a real assertion too (both files parse, both skins have the expected
// joint count) -- it doubles as "the fixtures a human checks are never
// stale relative to the current exporter", not just a one-off script run
// once and forgotten. ONYX_BUILD_DIR is injected by Tests/CMakeLists.txt
// (target_compile_definitions) so this lands at build/gltf-check/
// regardless of ctest's own default working directory for this test.
TEST_CASE("GltfExport: human validation fixtures land in build/gltf-check") {
    auto dir = std::filesystem::path(ONYX_BUILD_DIR) / "gltf-check";
    std::filesystem::create_directories(dir);

    {
        CorpusScene cs = BuildSkinnedCube();
        auto path = dir / "skinned-cube.glb";
        std::string err;
        REQUIRE(ExportSceneData(cs.scene, path, GltfOptions{}, err));
        cgltf_data* data = ParseOrFail(path);
        CHECK(cgltf_validate(data) == cgltf_result_success);
        REQUIRE(data->skins_count == 1);
        CHECK(data->skins[0].joints_count == 3);
        cgltf_free(data);
    }
    {
        CorpusScene cs = BuildJointChain200();
        auto path = dir / "joint-chain-200.glb";
        std::string err;
        REQUIRE(ExportSceneData(cs.scene, path, GltfOptions{}, err));
        cgltf_data* data = ParseOrFail(path);
        CHECK(cgltf_validate(data) == cgltf_result_success);
        REQUIRE(data->skins_count == 1);
        CHECK(data->skins[0].joints_count == 200);
        cgltf_free(data);
    }
}

TEST_CASE("GltfExport: joint-chain-200 exports all 200 joints with matching skin joint count") {
    CorpusScene cs = BuildJointChain200();
    auto path = TempPath("onyx_gltf_joint_chain_200.glb");
    std::filesystem::remove(path);

    std::string err;
    REQUIRE(ExportSceneData(cs.scene, path, GltfOptions{}, err));

    cgltf_data* data = ParseOrFail(path);
    CHECK(cgltf_validate(data) == cgltf_result_success);
    REQUIRE(data->skins_count == 1);
    CHECK(data->skins[0].joints_count == 200);
    CHECK(data->meshes[0].primitives_count == cs.scene.meshParts.size());

    cgltf_free(data);
    std::filesystem::remove(path);
}
