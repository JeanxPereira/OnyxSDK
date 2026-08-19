#pragma once

// See VkContext.h for the binding include-order rule (volk.h, then
// vk_mem_alloc.h, before any other Vulkan-touching header). RenderContext
// only needs the raw handle types (VkDevice/VkQueue/VkCommandBuffer/
// VmaAllocator) FrameHandles bundles below -- not the VkContext class
// itself -- so it repeats VkContext.h's own direct include pair here rather
// than pulling that unrelated class in just to honor the ordering rule.
#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Onyx::Rendering {

/// The raw Vulkan handles one frame hands to every registered pass -- spec
/// §8's "raw floor": no translation layer, no Onyx-owned wrapper, just what
/// a consuming app needs to record its own commands into this frame. `cmd`
/// is already inside the scene's dynamic-rendering pass when
/// RenderContext::Execute invokes a pass with it (after the scene draw,
/// before UI -- see that method's doc comment), so a pass may record any
/// command legal there (pipeline binds, draws, vkCmdClearAttachments,
/// etc.), but must not itself begin/end the command buffer or the dynamic-
/// rendering scope -- both are the frame owner's job, not a pass's.
struct FrameHandles {
    VkDevice        device;
    VkQueue         graphicsQueue;
    VkCommandBuffer cmd;
    uint32_t        graphicsFamily;
    VmaAllocator    allocator;
};

/// Spec §8's raw floor: lets a consuming app record its own Vulkan commands
/// into the frame's command buffer with zero translation layer. Registered
/// passes run in AddPass() order, after the scene draw and before UI -- the
/// Shell's per-frame Execute() call (landing in T9) sits exactly there.
///
/// Exception containment (spec §7.1): a pass callback that throws is caught
/// inside Execute() -- both a `catch (const std::exception&)` arm and a
/// bare `catch (...)` arm -- logged via the existing Logger (LOG_WARN,
/// naming the pass), and SKIPPED for this frame only. The pass is NOT
/// removed: a transient throw (e.g. one frame's worth of bad data) must not
/// silently unregister a pass that could succeed on the next frame. Every
/// other registered pass still runs, in registration order, regardless of
/// where in the sequence one throws.
///
/// "Skipped" describes what happens AFTER the throw, not before: any
/// command the pass already recorded into `handles.cmd` before throwing is
/// NOT rolled back -- Vulkan has no such mechanism, and this class does not
/// attempt to fake one (e.g. via a secondary command buffer it could discard
/// on failure). Containment guarantees the frame and every other registered
/// pass survive; it does not guarantee the throwing pass's own partial
/// recording never happened. A pass that mutates `cmd` state before its
/// throw point (binds a pipeline, writes a push constant, etc.) leaves that
/// state in place for whatever runs after it this frame.
///
/// Not copyable: matches every other RenderVk class's convention
/// (OffscreenTarget, SceneRendererVk) even though this one owns no GPU
/// resource itself, just registered callbacks -- there is no use case for
/// copying or moving a live registry mid-frame.
class RenderContext {
public:
    RenderContext() = default;

    RenderContext(const RenderContext&) = delete;
    RenderContext& operator=(const RenderContext&) = delete;

    /// Registers `fn` under `name` (used only for the contained-throw log
    /// line above) and returns an id RemovePass() can use to unregister it
    /// later. Ids are unique for the lifetime of this object: assigned from
    /// a monotonically increasing counter, never reused even after a
    /// RemovePass() call, so a caller can never accidentally remove a
    /// different, later pass that happened to reuse an old id.
    int AddPass(std::string name, std::function<void(const FrameHandles&)> fn);

    /// Unregisters the pass with this id. A no-op if no pass with that id is
    /// currently registered (already removed, or the id never existed) --
    /// callers don't need to track whether they already called this.
    void RemovePass(int id);

    /// Invokes every registered pass, in registration order, with
    /// `handles`. Called once per frame by the frame owner (the Shell, from
    /// T9), while `handles.cmd` is still inside the scene's dynamic-
    /// rendering pass, after the scene draw and before UI. See the class
    /// doc comment for the exception-containment contract -- in particular,
    /// a throwing pass's commands already recorded into `handles.cmd`
    /// before the throw are NOT rolled back; only the frame and the other
    /// passes are guaranteed to survive.
    void Execute(const FrameHandles& handles);

private:
    struct Pass {
        int                                       id;
        std::string                               name;
        std::function<void(const FrameHandles&)>  fn;
    };

    std::vector<Pass> m_passes;
    int                m_nextId = 1;
};

} // namespace Onyx::Rendering
