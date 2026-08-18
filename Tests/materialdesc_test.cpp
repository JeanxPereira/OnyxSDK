// ── MaterialDesc role resolution tests (doctest) ──────────────────────────
//
// ResolveRoleIndices is a pure helper (no GL calls) that maps a MaterialDesc's
// sparse TextureRole -> pool-index map onto a fixed enum-ordered array, -1
// where a role is absent. It is declared in SceneRenderer.h precisely so it
// can be exercised here without a GL context.

#include <doctest/doctest.h>

#include <Onyx/Rendering/SceneRenderer.h>
#include <Onyx/Parsers/SceneNode.h>

using namespace Onyx::Rendering;
using namespace Onyx::Parsers;

TEST_CASE("ResolveRoleIndices: empty MaterialDesc resolves every role to -1") {
    MaterialDesc mat;

    auto roles = SceneRenderer::ResolveRoleIndices(mat);

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

    auto roles = SceneRenderer::ResolveRoleIndices(mat);

    for (size_t i = 0; i < roles.size(); ++i) {
        CHECK(roles[i] == (int)i);
    }
}

TEST_CASE("ResolveRoleIndices: sparse map resolves only the roles present") {
    MaterialDesc mat;
    mat.textures[TextureRole::Diffuse] = 3;
    mat.textures[TextureRole::Gloss]   = 0;

    auto roles = SceneRenderer::ResolveRoleIndices(mat);

    CHECK(roles[(size_t)TextureRole::Diffuse] == 3);
    CHECK(roles[(size_t)TextureRole::Gloss]   == 0);

    for (size_t i = 0; i < roles.size(); ++i) {
        if (i == (size_t)TextureRole::Diffuse || i == (size_t)TextureRole::Gloss)
            continue;
        CHECK(roles[i] == -1);
    }
}
