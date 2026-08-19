#include <Onyx/Rendering/RenderBatch.h>

namespace Onyx::Rendering {

// ── Material role resolution ─────────────────────────────────────────────
// Pure — no GL calls, no Vulkan calls — so this is exercised directly in
// Tests/materialdesc_test.cpp without any GPU context. Extracted verbatim
// (Task 11) from the deleted Source/Rendering/SceneRenderer.cpp, where it
// lived as the static method Onyx::Rendering::SceneRenderer::
// ResolveRoleIndices — see RenderBatch.h's own doc comment for why.

std::array<int, 9> ResolveRoleIndices(const Parsers::MaterialDesc& mat) {
    std::array<int, 9> roles;
    roles.fill(-1);
    for (const auto& [role, texIndex] : mat.textures) {
        // A module can hand us a MaterialDesc built from a bogus/out-of-range
        // int cast into TextureRole (corrupt or hostile asset data). Guard
        // the write so that never walks past the fixed 9-slot array.
        if ((size_t)role < roles.size()) {
            roles[(size_t)role] = texIndex;
        }
    }
    return roles;
}

} // namespace Onyx::Rendering
