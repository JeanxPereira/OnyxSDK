// Pure, no-device tests for T3's embedded SPIR-V (cmake/ShaderCompile.cmake's
// onyx_add_spirv, wired for the Onyx_RenderVk target in the root
// CMakeLists.txt). Every generated `<name>_spv.h` header must produce a
// non-empty `constexpr uint32_t` array whose first word is the SPIR-V magic
// number -- no VkContext, no device, nothing Vulkan-runtime touched here.
//
// Also T8's pure registry tests for RenderContext (Include/Onyx/RenderVk/
// RenderContext.h) -- AddPass id uniqueness, RemovePass, registration
// order, and exception containment. None of this touches a real VkDevice:
// RenderContext::Execute never dereferences a single field of the
// FrameHandles it is handed, only forwards the reference into each
// registered callback, so a default-initialized (all-null-handle)
// FrameHandles exercises the exact same code path a real frame's handles
// would. The device-gated proof that a pass actually records GPU commands
// into a real frame (vkCmdClearAttachments tinting a corner) lives in
// onyx-oracle's --vk-scene-smoke instead (Tools/OnyxOracle/Main.cpp) --
// see Tests/CMakeLists.txt's OnyxRenderContext/CTest wiring note.
#include <doctest/doctest.h>

#include <Onyx/RenderVk/RenderContext.h>

#include "scene_vert_spv.h"
#include "scene_frag_spv.h"
#include "grid_vert_spv.h"
#include "grid_frag_spv.h"
#include "background_vert_spv.h"
#include "background_frag_spv.h"
#include "overlay_vert_spv.h"
#include "overlay_frag_spv.h"

#include <stdexcept>
#include <vector>

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

TEST_CASE("RenderVkSpirv: overlay.vert compiles to non-empty SPIR-V with the correct magic") {
    using namespace Onyx::RenderVk::Shaders;
    CHECK(kOverlayVertSpvSize > 0);
    CHECK(sizeof(kOverlayVertSpv) == kOverlayVertSpvSize);
    CHECK(kOverlayVertSpv[0] == kSpirvMagic);
}

TEST_CASE("RenderVkSpirv: overlay.frag compiles to non-empty SPIR-V with the correct magic") {
    using namespace Onyx::RenderVk::Shaders;
    CHECK(kOverlayFragSpvSize > 0);
    CHECK(sizeof(kOverlayFragSpv) == kOverlayFragSpvSize);
    CHECK(kOverlayFragSpv[0] == kSpirvMagic);
}

// ── RenderContext: pure registry tests (T8) ──────────────────────────────

TEST_CASE("RenderContext: AddPass returns unique, monotonically increasing ids") {
    Onyx::RenderVk::RenderContext ctx;
    int a = ctx.AddPass("a", [](const Onyx::RenderVk::FrameHandles&) {});
    int b = ctx.AddPass("b", [](const Onyx::RenderVk::FrameHandles&) {});
    int c = ctx.AddPass("c", [](const Onyx::RenderVk::FrameHandles&) {});

    CHECK(a != b);
    CHECK(b != c);
    CHECK(a != c);
    CHECK(a < b);
    CHECK(b < c);

    // RemovePass never frees an id for reuse -- a fresh AddPass after a
    // removal must still hand out an id no earlier pass (live or removed)
    // ever held.
    ctx.RemovePass(b);
    int d = ctx.AddPass("d", [](const Onyx::RenderVk::FrameHandles&) {});
    CHECK(d != a);
    CHECK(d != b);
    CHECK(d != c);
}

TEST_CASE("RenderContext: Execute runs every pass exactly once, in registration order") {
    Onyx::RenderVk::RenderContext ctx;
    std::vector<int> order;
    ctx.AddPass("first",  [&](const Onyx::RenderVk::FrameHandles&) { order.push_back(1); });
    ctx.AddPass("second", [&](const Onyx::RenderVk::FrameHandles&) { order.push_back(2); });
    ctx.AddPass("third",  [&](const Onyx::RenderVk::FrameHandles&) { order.push_back(3); });

    Onyx::RenderVk::FrameHandles handles{}; // all-null-handle -- Execute never dereferences it
    ctx.Execute(handles);

    REQUIRE(order.size() == 3);
    CHECK(order[0] == 1);
    CHECK(order[1] == 2);
    CHECK(order[2] == 3);
}

TEST_CASE("RenderContext: RemovePass unregisters a pass; unknown/repeat ids are a no-op") {
    Onyx::RenderVk::RenderContext ctx;
    std::vector<int> order;
    int a = ctx.AddPass("a", [&](const Onyx::RenderVk::FrameHandles&) { order.push_back(1); });
    int b = ctx.AddPass("b", [&](const Onyx::RenderVk::FrameHandles&) { order.push_back(2); });
    int c = ctx.AddPass("c", [&](const Onyx::RenderVk::FrameHandles&) { order.push_back(3); });

    ctx.RemovePass(b);
    // Removing an id that no longer refers to a live pass, and an id that
    // never existed, must both be silent no-ops -- neither should throw or
    // touch the passes still registered.
    ctx.RemovePass(b);
    ctx.RemovePass(9999);

    Onyx::RenderVk::FrameHandles handles{};
    ctx.Execute(handles);

    REQUIRE(order.size() == 2);
    CHECK(order[0] == 1);
    CHECK(order[1] == 3);
    (void)a;
    (void)c;
}

TEST_CASE("RenderContext: a throwing pass is caught, skipped for that frame, and not removed; "
          "other passes still run") {
    Onyx::RenderVk::RenderContext ctx;
    std::vector<int> order;

    ctx.AddPass("before", [&](const Onyx::RenderVk::FrameHandles&) { order.push_back(1); });
    int throwing = ctx.AddPass("throws-std-exception",
                                [&](const Onyx::RenderVk::FrameHandles&) {
                                    throw std::runtime_error("boom");
                                });
    ctx.AddPass("after", [&](const Onyx::RenderVk::FrameHandles&) { order.push_back(3); });

    Onyx::RenderVk::FrameHandles handles{};

    // Execute itself must never propagate the pass's throw.
    CHECK_NOTHROW(ctx.Execute(handles));

    REQUIRE(order.size() == 2);
    CHECK(order[0] == 1);
    CHECK(order[1] == 3);

    // A transient throw does not unregister the pass -- RemovePass(id)
    // still finds and removes it (proving it was never silently dropped
    // from the registry), and a second Execute() still invokes the OTHER
    // passes exactly as before.
    order.clear();
    CHECK_NOTHROW(ctx.Execute(handles));
    REQUIRE(order.size() == 2);

    ctx.RemovePass(throwing);
    order.clear();
    CHECK_NOTHROW(ctx.Execute(handles));
    REQUIRE(order.size() == 2); // before + after -- the (now-removed) throwing pass never ran
}

TEST_CASE("RenderContext: a pass that throws a non-std::exception is also caught and skipped") {
    Onyx::RenderVk::RenderContext ctx;
    std::vector<int> order;

    ctx.AddPass("before", [&](const Onyx::RenderVk::FrameHandles&) { order.push_back(1); });
    ctx.AddPass("throws-int", [&](const Onyx::RenderVk::FrameHandles&) { throw 42; });
    ctx.AddPass("after", [&](const Onyx::RenderVk::FrameHandles&) { order.push_back(3); });

    Onyx::RenderVk::FrameHandles handles{};
    CHECK_NOTHROW(ctx.Execute(handles));

    REQUIRE(order.size() == 2);
    CHECK(order[0] == 1);
    CHECK(order[1] == 3);
}
