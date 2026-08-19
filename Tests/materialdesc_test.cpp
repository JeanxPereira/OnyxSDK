// ── MaterialDesc role resolution tests (doctest) ──────────────────────────
//
// ResolveRoleIndices is a pure helper (no GL calls, no Vulkan calls) that
// maps a MaterialDesc's sparse TextureRole -> pool-index map onto a fixed
// enum-ordered array, -1 where a role is absent. It is declared in
// RenderBatch.h (Task 11: extracted from the now-deleted GL SceneRenderer.h,
// see that header's own top comment) precisely so it can be exercised here
// without any GPU context.

#include <doctest/doctest.h>

#include <Onyx/Rendering/RenderBatch.h>
#include <Onyx/Parsers/SceneNode.h>

using namespace Onyx::Rendering;
using namespace Onyx::Parsers;

TEST_CASE("ResolveRoleIndices: empty MaterialDesc resolves every role to -1") {
    MaterialDesc mat;

    auto roles = ResolveRoleIndices(mat);

    for (int idx : roles) {
        CHECK(idx == -1);
    }
}

TEST_CASE("ResolveRoleIndices: full 9-role map echoes each slot's own index") {
    MaterialDesc mat;
    mat.textures[TextureRole::Diffuse]  = 0;
    mat.textures[TextureRole::Normal]   = 1;
    mat.textures[TextureRole::Occlusion] = 2;
    mat.textures[TextureRole::Gloss]    = 3;
    mat.textures[TextureRole::Height]   = 4;
    mat.textures[TextureRole::Scatter]  = 5;
    mat.textures[TextureRole::Detail]   = 6;
    mat.textures[TextureRole::Emissive] = 7;
    mat.textures[TextureRole::EnvMap]   = 8;

    auto roles = ResolveRoleIndices(mat);

    for (size_t i = 0; i < roles.size(); ++i) {
        CHECK(roles[i] == (int)i);
    }
}

TEST_CASE("ResolveRoleIndices: sparse map resolves only the roles present") {
    MaterialDesc mat;
    mat.textures[TextureRole::Diffuse] = 3;
    mat.textures[TextureRole::Gloss]   = 0;

    auto roles = ResolveRoleIndices(mat);

    CHECK(roles[(size_t)TextureRole::Diffuse] == 3);
    CHECK(roles[(size_t)TextureRole::Gloss]   == 0);

    for (size_t i = 0; i < roles.size(); ++i) {
        if (i == (size_t)TextureRole::Diffuse || i == (size_t)TextureRole::Gloss)
            continue;
        CHECK(roles[i] == -1);
    }
}

TEST_CASE("ResolveRoleIndices: an out-of-range TextureRole is ignored, not written OOB") {
    MaterialDesc mat;
    mat.textures[TextureRole::Diffuse] = 1;
    mat.textures[static_cast<TextureRole>(42)] = 99;  // bogus role from corrupt/hostile data
    mat.textures[TextureRole::EnvMap] = 8;

    auto roles = ResolveRoleIndices(mat);

    // The 9 valid slots are unaffected; no crash / OOB write happened.
    CHECK(roles[(size_t)TextureRole::Diffuse] == 1);
    CHECK(roles[(size_t)TextureRole::EnvMap]  == 8);
    for (size_t i = 0; i < roles.size(); ++i) {
        if (i == (size_t)TextureRole::Diffuse || i == (size_t)TextureRole::EnvMap)
            continue;
        CHECK(roles[i] == -1);
    }
}
