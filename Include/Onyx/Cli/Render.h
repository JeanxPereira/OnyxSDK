#pragma once

// CmdRender -- headless "decode a Scene entry and rasterize it to a PNG"
// CLI command (M4 Task 14).
//
// ── Why this is NOT in Commands.h/Commands.cpp ─────────────────────────────
// Commands.cpp compiles into Onyx_Core (root CMakeLists.txt's
// ONYX_CORE_SOURCES list), documented at that target's own declaration as
// "no UI, no GPU". Every renderer library in this codebase depends on
// Onyx_Core (Onyx_Render links it PUBLIC; Onyx_RenderVk links it PUBLIC
// too -- see CMakeLists.txt's `target_link_libraries(Onyx_RenderVk PUBLIC
// Onyx_Core ...)`). Giving Commands.cpp a Vulkan dependency (VkContext,
// Pipelines, SceneRendererVk, OffscreenTarget) to implement `render` would
// mean Onyx_Core links Onyx_RenderVk -- and Onyx_RenderVk already links
// Onyx_Core, so that direction of dependency is a real CMake link cycle,
// not just an architectural smell that LayerGuard would eventually catch.
//
// CmdRender instead lives in its own translation unit, Examples/OnyxCli/
// Render.cpp -- deliberately NOT Source/Cli/ despite this header's own
// location under Include/Onyx/Cli/: root CMakeLists.txt's layer-
// completeness check globs Source/*.cpp and requires every match be
// claimed by exactly one of the five ONYX_*_SOURCES lists (Core/Render/
// Media/Shell/RenderVk); a file meant to compile straight into an
// executable and claimed by none of them fails that configure-time check
// the instant it lives under Source/. Examples/ sits outside that glob.
// Render.cpp is compiled directly into the onyxbox-cli executable
// (Examples/OnyxCli/CMakeLists.txt) rather than into any static library --
// the executable is the one place Onyx_Core and Onyx::RenderVk may safely
// meet, since an executable has no further consumers to leak the cycle
// into. Run()'s probe/list/extract/decode dispatch (Source/Cli/
// Commands.cpp) stays entirely Vulkan-free; Examples/OnyxCli/Main.cpp
// special-cases "render" itself (parses --out/--width/--height, calls
// CmdRender directly) before falling through to Onyx::Cli::Run() for every
// other subcommand -- see that file's own comment for the exact dispatch
// shape.
//
// ── Exit codes ──────────────────────────────────────────────────────────
// Reuses Onyx::Cli::kOk/kUsage/kNoModule from Commands.h (0/1/2) for the
// same usage-error taxonomy CmdDecode already established. PLUS one
// convention borrowed from Tools/OnyxOracle: exit 77 when no Vulkan-capable
// device/driver is present. 77 is deliberately not one of Commands.h's
// named constants -- it belongs to a different axis entirely (environment
// capability, not caller error), exactly like every onyx-oracle Vk* entry
// point's own 77. A caller (ctest, CI) checks for it explicitly and treats
// it as SKIP, not FAIL.

#include <Onyx/Cli/Commands.h>       // kOk/kUsage/kNoModule -- reused, not redefined
#include <Onyx/Modules/Workspace.h>

#include <filesystem>
#include <iosfwd>
#include <string_view>

namespace Onyx::Cli {

// Resolves `entryName` against the document opened from `path` exactly
// like CmdDecode does (first name match, pre-order depth-first search over
// the opened document's tree -- see Source/Cli/Commands.cpp's
// FindEntryByName, and this command's own Examples/OnyxCli/Render.cpp copy
// of it), decodes it through the Scene capability, renders it headlessly
// through Onyx::RenderVk::SceneRendererVk into a `width` x `height`
// OffscreenTarget (a fixed camera framing the decoded scene's object-space
// bounding box with margin -- see Render.cpp's top comment), and writes
// the result as `outPng` (RGBA8 PNG via stb_image_write) plus a sibling
// `<outPng with .json extension>` minimal report (entry name, dimensions,
// part/material/vertex counts) -- NOT Tools/OnyxOracle/RenderReport.h's
// BuildReport() shape; see Render.cpp's top comment for why this command
// does not reuse it. width/height default to 512 when <= 0 is passed.
//
// Returns kNoModule if no module accepts `path`; kUsage if the entry does
// not exist, has no Scene decode capability, the decoder salvage-fails, or
// any later I/O/GPU-resource step fails for a reason that is not "no
// Vulkan device" (Commands.h's contract has no dedicated code for that
// case, so it shares kUsage -- diags/stderr explain which); kOk on a
// successful render. May also exit 77 (checked separately from the
// kOk/kUsage/kNoModule/kStrictErrors family -- see this header's top
// comment) when Onyx::RenderVk::VkContext::Init finds no Vulkan-capable
// device/driver.
int CmdRender(Modules::Workspace& ws, const std::filesystem::path& path,
              std::string_view entryName, const std::filesystem::path& outPng,
              int width, int height, std::ostream& out, std::string_view moduleHint = {});

} // namespace Onyx::Cli
