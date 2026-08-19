#pragma once

// CmdRender -- headless "decode a Scene entry and rasterize it to a PNG"
// CLI command (M4 Task 14; moved into a shipped library at M5 Task 6 --
// see the section below).
//
// ── Why this is NOT in Commands.h/Commands.cpp ─────────────────────────────
// Commands.cpp compiles into Onyx_Core (root CMakeLists.txt's
// ONYX_CORE_SOURCES list), documented at that target's own declaration as
// "no UI, no GPU". The renderer library depends on Onyx_Core (Onyx_Render
// links it PUBLIC -- see CMakeLists.txt's `target_link_libraries(Onyx_Render
// PUBLIC Onyx_Core ...)`; the GL Onyx_Render used to as well, before Task 11
// deleted it). Giving Commands.cpp a Vulkan dependency (VkContext,
// Pipelines, SceneRendererVk, OffscreenTarget) to implement `render` would
// mean Onyx_Core links Onyx_Render -- and Onyx_Render already links
// Onyx_Core, so that direction of dependency is a real CMake link cycle,
// not just an architectural smell that LayerGuard would eventually catch.
//
// ── Where CmdRender actually lives (M5 Task 6 fix for audit finding G1) ────
// M4 put the implementation, Render.cpp, directly into the onyxbox-cli
// EXECUTABLE (Examples/OnyxCli/) rather than any static library, reasoning
// that an executable is the one place Onyx_Core and Onyx::Render may safely
// meet since it has no further consumers to leak the cycle into. That is
// true, but it also meant this header -- public, under Include/Onyx/Cli/ --
// declared a symbol that shipped in NO library: a third-party toolkit that
// included Render.h and linked every documented Onyx_* target still got an
// unresolved external at link time. Searching every archive under build/
// for the mangled symbol proved it (docs/design/2026-08-19-public-surface-
// audit.md, G1): `CmdDecode` (declared next door in Commands.h) resolves
// out of Onyx_Core.lib; `CmdRender` resolved out of nothing.
//
// The fix is the audit's own suggestion: a fourth library, above both
// Onyx_Core and Onyx_Render, that owns Render.cpp and links both PUBLIC --
// cycle-free because NEITHER Onyx_Core nor Onyx_Render depends on it, only
// the other way around. That library is Onyx_CliRender (alias
// Onyx::CliRender, root CMakeLists.txt), and Render.cpp now lives at
// Source/Cli/Render.cpp -- inside the layer-completeness glob this time,
// because it is finally claimed by a real ONYX_*_SOURCES list
// (ONYX_CLIRENDER_SOURCES) instead of being a file compiled straight into
// one specific executable. A consumer that links Onyx::CliRender (which
// transitively pulls in Onyx::Render and Onyx::Core -- see the CMake
// target's own PUBLIC link libraries) gets a working `CmdRender` symbol
// with no example source compiled into their own build. This is shape (a)
// of the two the M5 Task 6 brief allowed; see Source/Cli/Render.cpp's own
// top comment for why it was chosen over shape (b) (an injected render
// callback, the shape Include/Onyx/Cli/Gltf.h's MakeGltfExportFn/
// SceneExportFn use for `decode --to gltf`) for THIS specific symbol.
//
// `Onyx::Cli::Run()`'s own argv dispatch (Commands.cpp, Onyx_Core) DOES use
// shape (b) for the `render` VERB specifically -- see Commands.h's
// `RenderFn` doc comment -- because Run() itself must stay inside Onyx_Core
// and therefore cannot call CmdRender by name. The two shapes are not in
// tension: CmdRender the SYMBOL ships in a linkable library (this file's
// own fix), and CmdRender the CALLER (Run()'s "render" argv parsing) reaches
// it through an injected hook a composition root binds to this exact
// function -- Examples/OnyxCli/Main.cpp passes `Onyx::Cli::CmdRender`
// itself as that hook, no wrapper needed, since RenderFn's signature is
// this function's own parameter list with defaults stripped.
//
// ── Exit codes ──────────────────────────────────────────────────────────
// Reuses Onyx::Cli::kOk/kUsage/kNoModule/kStrictErrors from Commands.h
// (0/1/2/3) for the same usage-error/strict-diag taxonomy CmdDecode already
// established -- kStrictErrors is new for `render` at M5 Task 6 (spec §11's
// `--strict` flag): the render itself still completes and the PNG still
// gets written, but the exit code tells a caller (CI, a script) that the
// document carried an Error diag. PLUS one convention borrowed from
// Tools/OnyxOracle: exit 77 when no Vulkan-capable device/driver is
// present. 77 is deliberately not one of Commands.h's named constants -- it
// belongs to a different axis entirely (environment capability, not caller
// error), exactly like every onyx-oracle Vk* entry point's own 77. A caller
// (ctest, CI) checks for it explicitly and treats it as SKIP, not FAIL.

#include <Onyx/Cli/Commands.h>       // kOk/kUsage/kNoModule/kStrictErrors, kCanonicalViews
#include <Onyx/Modules/Workspace.h>

#include <filesystem>
#include <iosfwd>
#include <string_view>

namespace Onyx::Cli {

// Resolves `entryName` against the document opened from `path` exactly
// like CmdDecode does (first name match, pre-order depth-first search over
// the opened document's tree -- see Source/Cli/Commands.cpp's
// FindEntryByName, and this command's own Source/Cli/Render.cpp copy of
// it), decodes it through the Scene capability, renders it headlessly
// through Onyx::Rendering::SceneRendererVk into a `width` x `height`
// OffscreenTarget (a fixed camera framing the decoded scene's object-space
// bounding box with margin, oriented per `view` -- see Render.cpp's top
// comment for the camera formula and kCanonicalViews for the full name
// set), and writes the result as `outPng` (RGBA8 PNG via stb_image_write)
// plus a sibling `<outPng with .json extension>` minimal report (entry
// name, view, dimensions, part/material/vertex counts) -- NOT
// Tools/OnyxOracle/RenderReport.h's BuildReport() shape; see Render.cpp's
// top comment for why this command does not reuse it. width/height
// default to 512 when <= 0 is passed. `view` defaults to "iso" -- the
// exact 45deg-yaw/15deg-pitch orbit this command used before --views
// existed, so a caller that never passes `view` explicitly sees
// byte-identical output to before M5 Task 6.
//
// Returns kNoModule if no module accepts `path`; kUsage if the entry does
// not exist, has no Scene decode capability, the decoder salvage-fails,
// `view` is not one of kCanonicalViews (Commands.h), or any later
// I/O/GPU-resource step fails for a reason that is not "no Vulkan device"
// (Commands.h's contract has no dedicated code for that case, so it shares
// kUsage -- diags/stderr explain which); kOk on a successful render with
// no Error diag, or when `strict` is false. Returns kStrictErrors instead
// of kOk when `strict` is true and the document's diags (drained after the
// Scene decode, same point CmdDecode itself checks) include any Error --
// the render still happens and outPng is still written; only the exit
// code changes. May also exit 77 (checked separately from the
// kOk/kUsage/kNoModule/kStrictErrors family -- see this header's top
// comment) when Onyx::Rendering::VkContext::Init finds no Vulkan-capable
// device/driver.
int CmdRender(Modules::Workspace& ws, const std::filesystem::path& path,
              std::string_view entryName, const std::filesystem::path& outPng,
              int width, int height, std::ostream& out, std::string_view moduleHint = {},
              std::string_view view = "iso", bool strict = false);

} // namespace Onyx::Cli
