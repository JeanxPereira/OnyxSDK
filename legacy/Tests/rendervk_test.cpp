// Pure, no-device tests for T3's embedded SPIR-V (cmake/ShaderCompile.cmake's
// onyx_add_spirv, wired for the Onyx_Render target in the root
// CMakeLists.txt). Every generated `<name>_spv.h` header must produce a
// non-empty `constexpr uint32_t` array whose first word is the SPIR-V magic
// number -- no VkContext, no device, nothing Vulkan-runtime touched here.
//
// Also T8's pure registry tests for RenderContext (Include/Onyx/Rendering/
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

#include <Onyx/Rendering/RenderContext.h>
#include <Onyx/Rendering/TexturePool.h>

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
    using namespace Onyx::Rendering::Shaders;
    CHECK(kSceneVertSpvSize > 0);
    CHECK(sizeof(kSceneVertSpv) == kSceneVertSpvSize);
    CHECK(kSceneVertSpv[0] == kSpirvMagic);
}

TEST_CASE("RenderVkSpirv: scene.frag compiles to non-empty SPIR-V with the correct magic") {
    using namespace Onyx::Rendering::Shaders;
    CHECK(kSceneFragSpvSize > 0);
    CHECK(sizeof(kSceneFragSpv) == kSceneFragSpvSize);
    CHECK(kSceneFragSpv[0] == kSpirvMagic);
}

TEST_CASE("RenderVkSpirv: grid.vert compiles to non-empty SPIR-V with the correct magic") {
    using namespace Onyx::Rendering::Shaders;
    CHECK(kGridVertSpvSize > 0);
    CHECK(sizeof(kGridVertSpv) == kGridVertSpvSize);
    CHECK(kGridVertSpv[0] == kSpirvMagic);
}

TEST_CASE("RenderVkSpirv: grid.frag compiles to non-empty SPIR-V with the correct magic") {
    using namespace Onyx::Rendering::Shaders;
    CHECK(kGridFragSpvSize > 0);
    CHECK(sizeof(kGridFragSpv) == kGridFragSpvSize);
    CHECK(kGridFragSpv[0] == kSpirvMagic);
}

TEST_CASE("RenderVkSpirv: background.vert compiles to non-empty SPIR-V with the correct magic") {
    using namespace Onyx::Rendering::Shaders;
    CHECK(kBackgroundVertSpvSize > 0);
    CHECK(sizeof(kBackgroundVertSpv) == kBackgroundVertSpvSize);
    CHECK(kBackgroundVertSpv[0] == kSpirvMagic);
}

TEST_CASE("RenderVkSpirv: background.frag compiles to non-empty SPIR-V with the correct magic") {
    using namespace Onyx::Rendering::Shaders;
    CHECK(kBackgroundFragSpvSize > 0);
    CHECK(sizeof(kBackgroundFragSpv) == kBackgroundFragSpvSize);
    CHECK(kBackgroundFragSpv[0] == kSpirvMagic);
}

TEST_CASE("RenderVkSpirv: overlay.vert compiles to non-empty SPIR-V with the correct magic") {
    using namespace Onyx::Rendering::Shaders;
    CHECK(kOverlayVertSpvSize > 0);
    CHECK(sizeof(kOverlayVertSpv) == kOverlayVertSpvSize);
    CHECK(kOverlayVertSpv[0] == kSpirvMagic);
}

TEST_CASE("RenderVkSpirv: overlay.frag compiles to non-empty SPIR-V with the correct magic") {
    using namespace Onyx::Rendering::Shaders;
    CHECK(kOverlayFragSpvSize > 0);
    CHECK(sizeof(kOverlayFragSpv) == kOverlayFragSpvSize);
    CHECK(kOverlayFragSpv[0] == kSpirvMagic);
}

// ── RenderContext: pure registry tests (T8) ──────────────────────────────

TEST_CASE("RenderContext: AddPass returns unique, monotonically increasing ids") {
    Onyx::Rendering::RenderContext ctx;
    int a = ctx.AddPass("a", [](const Onyx::Rendering::FrameHandles&) {});
    int b = ctx.AddPass("b", [](const Onyx::Rendering::FrameHandles&) {});
    int c = ctx.AddPass("c", [](const Onyx::Rendering::FrameHandles&) {});

    CHECK(a != b);
    CHECK(b != c);
    CHECK(a != c);
    CHECK(a < b);
    CHECK(b < c);

    // RemovePass never frees an id for reuse -- a fresh AddPass after a
    // removal must still hand out an id no earlier pass (live or removed)
    // ever held.
    ctx.RemovePass(b);
    int d = ctx.AddPass("d", [](const Onyx::Rendering::FrameHandles&) {});
    CHECK(d != a);
    CHECK(d != b);
    CHECK(d != c);
}

TEST_CASE("RenderContext: Execute runs every pass exactly once, in registration order") {
    Onyx::Rendering::RenderContext ctx;
    std::vector<int> order;
    ctx.AddPass("first",  [&](const Onyx::Rendering::FrameHandles&) { order.push_back(1); });
    ctx.AddPass("second", [&](const Onyx::Rendering::FrameHandles&) { order.push_back(2); });
    ctx.AddPass("third",  [&](const Onyx::Rendering::FrameHandles&) { order.push_back(3); });

    Onyx::Rendering::FrameHandles handles{}; // all-null-handle -- Execute never dereferences it
    ctx.Execute(handles);

    REQUIRE(order.size() == 3);
    CHECK(order[0] == 1);
    CHECK(order[1] == 2);
    CHECK(order[2] == 3);
}

TEST_CASE("RenderContext: RemovePass unregisters a pass; unknown/repeat ids are a no-op") {
    Onyx::Rendering::RenderContext ctx;
    std::vector<int> order;
    int a = ctx.AddPass("a", [&](const Onyx::Rendering::FrameHandles&) { order.push_back(1); });
    int b = ctx.AddPass("b", [&](const Onyx::Rendering::FrameHandles&) { order.push_back(2); });
    int c = ctx.AddPass("c", [&](const Onyx::Rendering::FrameHandles&) { order.push_back(3); });

    ctx.RemovePass(b);
    // Removing an id that no longer refers to a live pass, and an id that
    // never existed, must both be silent no-ops -- neither should throw or
    // touch the passes still registered.
    ctx.RemovePass(b);
    ctx.RemovePass(9999);

    Onyx::Rendering::FrameHandles handles{};
    ctx.Execute(handles);

    REQUIRE(order.size() == 2);
    CHECK(order[0] == 1);
    CHECK(order[1] == 3);
    (void)a;
    (void)c;
}

TEST_CASE("RenderContext: a throwing pass is caught, skipped for that frame, and not removed; "
          "other passes still run") {
    Onyx::Rendering::RenderContext ctx;
    std::vector<int> order;

    ctx.AddPass("before", [&](const Onyx::Rendering::FrameHandles&) { order.push_back(1); });
    int throwing = ctx.AddPass("throws-std-exception",
                                [&](const Onyx::Rendering::FrameHandles&) {
                                    throw std::runtime_error("boom");
                                });
    ctx.AddPass("after", [&](const Onyx::Rendering::FrameHandles&) { order.push_back(3); });

    Onyx::Rendering::FrameHandles handles{};

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
    Onyx::Rendering::RenderContext ctx;
    std::vector<int> order;

    ctx.AddPass("before", [&](const Onyx::Rendering::FrameHandles&) { order.push_back(1); });
    ctx.AddPass("throws-int", [&](const Onyx::Rendering::FrameHandles&) { throw 42; });
    ctx.AddPass("after", [&](const Onyx::Rendering::FrameHandles&) { order.push_back(3); });

    Onyx::Rendering::FrameHandles handles{};
    CHECK_NOTHROW(ctx.Execute(handles));

    REQUIRE(order.size() == 2);
    CHECK(order[0] == 1);
    CHECK(order[1] == 3);
}

// ── DeferredDestroyQueue: pure N-frames-in-flight bookkeeping (T10) ─────

TEST_CASE("DeferredDestroyQueue: an entry does not run before framesInFlight frames elapse") {
    Onyx::Rendering::DeferredDestroyQueue q;
    int destroyedCount = 0;
    q.Retire(/*retiredFrame=*/10, [&] { ++destroyedCount; });

    // Same frame as retirement: not safe yet.
    q.Collect(10, /*framesInFlight=*/2);
    CHECK(destroyedCount == 0);
    CHECK(q.PendingCount() == 1);

    // One frame later: distance is 1 < framesInFlight(2) -- still not safe.
    q.Collect(11, 2);
    CHECK(destroyedCount == 0);
    CHECK(q.PendingCount() == 1);

    // Two frames later: distance is exactly framesInFlight -- now safe.
    q.Collect(12, 2);
    CHECK(destroyedCount == 1);
    CHECK(q.PendingCount() == 0);
}

TEST_CASE("DeferredDestroyQueue: Collect is idempotent once an entry has already run") {
    Onyx::Rendering::DeferredDestroyQueue q;
    int destroyedCount = 0;
    q.Retire(0, [&] { ++destroyedCount; });

    q.Collect(100, 2); // way past due -- runs immediately
    CHECK(destroyedCount == 1);

    // Calling again must not re-run the same (already-removed) entry.
    q.Collect(200, 2);
    CHECK(destroyedCount == 1);
}

TEST_CASE("DeferredDestroyQueue: entries run in Retire() order, only the ones that are due") {
    Onyx::Rendering::DeferredDestroyQueue q;
    std::vector<int> ranOrder;
    q.Retire(0, [&] { ranOrder.push_back(1); });
    q.Retire(5, [&] { ranOrder.push_back(2); }); // not due yet at currentFrame=6, framesInFlight=2
    q.Retire(0, [&] { ranOrder.push_back(3); });

    q.Collect(6, 2);

    REQUIRE(ranOrder.size() == 2);
    CHECK(ranOrder[0] == 1);
    CHECK(ranOrder[1] == 3);
    REQUIRE(q.PendingCount() == 1); // entry retired at frame 5 is still pending
}

TEST_CASE("DeferredDestroyQueue: framesInFlight == 0 fires as soon as currentFrame reaches retiredFrame") {
    Onyx::Rendering::DeferredDestroyQueue q;
    int destroyedCount = 0;
    q.Retire(3, [&] { ++destroyedCount; });

    q.Collect(2, 0);
    CHECK(destroyedCount == 0);

    q.Collect(3, 0);
    CHECK(destroyedCount == 1);
}

TEST_CASE("DeferredDestroyQueue: CollectAll runs every pending entry unconditionally") {
    Onyx::Rendering::DeferredDestroyQueue q;
    int destroyedCount = 0;
    q.Retire(1000, [&] { ++destroyedCount; }); // would never be due at currentFrame=0
    q.Retire(2000, [&] { ++destroyedCount; });

    CHECK(q.PendingCount() == 2);
    q.CollectAll();
    CHECK(destroyedCount == 2);
    CHECK(q.PendingCount() == 0);
}

TEST_CASE("DeferredDestroyQueue: a queue with nothing retired is a no-op on Collect/CollectAll") {
    Onyx::Rendering::DeferredDestroyQueue q;
    CHECK_NOTHROW(q.Collect(42, 2));
    CHECK_NOTHROW(q.CollectAll());
    CHECK(q.PendingCount() == 0);
}
