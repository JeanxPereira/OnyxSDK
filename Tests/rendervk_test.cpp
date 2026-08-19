// Pure, no-device tests for T3's embedded SPIR-V (cmake/ShaderCompile.cmake's
// onyx_add_spirv, wired for the Onyx_RenderVk target in the root
// CMakeLists.txt). Every generated `<name>_spv.h` header must produce a
// non-empty `constexpr uint32_t` array whose first word is the SPIR-V magic
// number -- no VkContext, no device, nothing Vulkan-runtime touched here.
#include <doctest/doctest.h>

#include "scene_vert_spv.h"
#include "scene_frag_spv.h"
#include "grid_vert_spv.h"
#include "grid_frag_spv.h"
#include "background_vert_spv.h"
#include "background_frag_spv.h"

namespace {
constexpr uint32_t kSpirvMagic = 0x07230203u;
}

TEST_CASE("RenderVkSpirv: scene.vert compiles to non-empty SPIR-V with the correct magic") {
    using namespace Onyx::RenderVk::Shaders;
    CHECK(kSceneVertSpvSize > 0);
    CHECK(sizeof(kSceneVertSpv) == kSceneVertSpvSize);
    CHECK(kSceneVertSpv[0] == kSpirvMagic);
}

TEST_CASE("RenderVkSpirv: scene.frag compiles to non-empty SPIR-V with the correct magic") {
    using namespace Onyx::RenderVk::Shaders;
    CHECK(kSceneFragSpvSize > 0);
    CHECK(sizeof(kSceneFragSpv) == kSceneFragSpvSize);
    CHECK(kSceneFragSpv[0] == kSpirvMagic);
}

TEST_CASE("RenderVkSpirv: grid.vert compiles to non-empty SPIR-V with the correct magic") {
    using namespace Onyx::RenderVk::Shaders;
    CHECK(kGridVertSpvSize > 0);
    CHECK(sizeof(kGridVertSpv) == kGridVertSpvSize);
    CHECK(kGridVertSpv[0] == kSpirvMagic);
}

TEST_CASE("RenderVkSpirv: grid.frag compiles to non-empty SPIR-V with the correct magic") {
    using namespace Onyx::RenderVk::Shaders;
    CHECK(kGridFragSpvSize > 0);
    CHECK(sizeof(kGridFragSpv) == kGridFragSpvSize);
    CHECK(kGridFragSpv[0] == kSpirvMagic);
}

TEST_CASE("RenderVkSpirv: background.vert compiles to non-empty SPIR-V with the correct magic") {
    using namespace Onyx::RenderVk::Shaders;
    CHECK(kBackgroundVertSpvSize > 0);
    CHECK(sizeof(kBackgroundVertSpv) == kBackgroundVertSpvSize);
    CHECK(kBackgroundVertSpv[0] == kSpirvMagic);
}

TEST_CASE("RenderVkSpirv: background.frag compiles to non-empty SPIR-V with the correct magic") {
    using namespace Onyx::RenderVk::Shaders;
    CHECK(kBackgroundFragSpvSize > 0);
    CHECK(sizeof(kBackgroundFragSpv) == kBackgroundFragSpvSize);
    CHECK(kBackgroundFragSpv[0] == kSpirvMagic);
}
