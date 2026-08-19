#pragma once
// Onyx SDK — renderer umbrella (audit gap G2 fix, sibling to Onyx.h).
//
// <Onyx/Onyx.h> deliberately leaves out the Vulkan-touching half of
// Onyx::Rendering (see that file's own top comment): every header below
// `#include`s volk.h/vk_mem_alloc.h directly or transitively, and the main
// umbrella is included by headless/CLI-only consumers that should never pay
// for Vulkan headers just to parse a container. A toolkit author who wants
// to drive the raw Vulkan floor directly (own VkContext, build pipelines,
// call SceneRendererVk::Build/Render, register a RenderContext pass) gets
// the whole surface from this one include instead.
//
// Pairs with the Onyx::Render target alias (Onyx_Render, root
// CMakeLists.txt) the same way <Onyx/Onyx.h> pairs with Onyx::Onyx --
// including this header without linking Onyx::Render (or the aggregate
// Onyx::Onyx, which already links it) leaves every symbol declared but
// unresolved at link time, same as any other Onyx header.
//
// Most callers do not need this at all: <Onyx/Rendering/RenderToImage.h>
// (already in <Onyx/Onyx.h>) is the ready-floor entry point spec §2
// promises -- "a Parsers::SceneData in, an RGBA image out" -- with zero
// Vulkan types in its own signature. Reach for THIS header only when the
// ready floor's one-shot-per-call contract does not fit (a persistent,
// multi-pass, GPU-resident viewport, e.g. Onyx::Viewers::Viewport3D's own
// implementation) -- see RenderToImage.h's top comment for that exact case.
//
// Third-party dependencies to compile a TU that includes ONLY this header
// (verified by a standalone cold-start compile, T8 fix round 1 -- an
// earlier version of this comment named only the Vulkan half and left
// AxisGizmo.h's own imgui.h dependency undocumented):
//   - glm (glm/glm.hpp)
//   - Vulkan-Headers (vulkan/vulkan.h, via volk.h/vk_mem_alloc.h)
//   - volk (volk.h)
//   - VulkanMemoryAllocator (vk_mem_alloc.h)
//   - Dear ImGui (imgui.h) -- pulled in by Rendering/AxisGizmo.h, which
//     draws its gizmo discs through ImGui's own ImDrawList (see that
//     header's own top comment); every consumer of this header links
//     Onyx::Render, which already links imgui_lib, so this is never an
//     extra dependency in practice -- it just was not written down here.
#include <Onyx/Rendering/VkContext.h>
#include <Onyx/Rendering/Pipelines.h>
#include <Onyx/Rendering/OffscreenTarget.h>
#include <Onyx/Rendering/RenderContext.h>
#include <Onyx/Rendering/SceneRendererVk.h>
#include <Onyx/Rendering/TexturePool.h>
#include <Onyx/Rendering/VkResources.h>

// The Vulkan-free half already ships in <Onyx/Onyx.h>, but pulling this
// header alone (without the main umbrella) should still be self-sufficient.
#include <Onyx/Rendering/RenderBatch.h>
#include <Onyx/Rendering/JointPalette.h>
#include <Onyx/Rendering/Camera.h>
#include <Onyx/Rendering/AnimationPlayer.h>
#include <Onyx/Rendering/AxisGizmo.h>
#include <Onyx/Rendering/RenderToImage.h>
