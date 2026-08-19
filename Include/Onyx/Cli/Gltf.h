#pragma once

// MakeGltfExportFn -- builds the SceneExportFn hook that lets `decode --to
// gltf` reach Onyx::Exchange::ExportSceneData (M5 Task 5, spec §9).
//
// ── Why this is NOT in Commands.h/Commands.cpp ─────────────────────────────
// Commands.cpp compiles into Onyx_Core (root CMakeLists.txt's
// ONYX_CORE_SOURCES list). Onyx_Exchange (Include/Onyx/Exchange/
// GltfExport.h) links Onyx_Core PUBLIC -- so if Commands.cpp linked
// Onyx_Exchange too, that would be a real CMake link cycle, not just a
// layering smell. This is the identical shape Include/Onyx/Cli/Render.h
// documents for CmdRender/Onyx::Render, one task earlier in this same
// milestone: the dependency direction that already exists (exporter/
// renderer -> Core) forbids Core from linking back.
//
// Commands.cpp still owns the actual `--to`/`--out` argv parsing and the
// "does this entry have Scene capability" gate (Onyx::Cli::CmdDecode's
// `toFormat`/`toOut`/`exportFn` parameters, Include/Onyx/Cli/Commands.h) --
// it just never calls Onyx::Exchange::ExportSceneData BY NAME. Instead it
// accepts an injected `SceneExportFn` hook (a `std::function` from
// SceneData + an output path to success/failure) that a composition root
// free to link both Onyx_Core and Onyx_Exchange supplies. MakeGltfExportFn
// below is exactly that composition root's implementation: it lives in
// Examples/OnyxCli/Gltf.cpp (compiled directly into the onyxbox-cli
// executable, NOT any static library -- same placement rule Render.cpp
// follows, and for the same reason: an executable has no further
// consumers to leak the cycle into), and returns a lambda wrapping
// `Onyx::Exchange::ExportSceneData` with a fixed `GltfOptions` for
// `Onyx::Cli::CmdDecode`/`Run` to call as their `exportFn` hook.
//
// Examples/OnyxCli/Main.cpp wires this in by passing MakeGltfExportFn(...)
// as Onyx::Cli::Run's `exportFn` parameter for every invocation, not just
// ones it knows ahead of time will use `--to gltf` -- CmdDecode itself
// only calls the hook when `--to` was actually given (see Commands.h's
// SceneExportFn doc comment), so passing it unconditionally costs nothing
// on every other subcommand.

#include <Onyx/Cli/Commands.h> // SceneExportFn

namespace Onyx::Cli {

// Returns a SceneExportFn bound to Onyx::Exchange::ExportSceneData with
// the given GltfOptions -- the one place in this executable that names
// Onyx::Exchange, so Main.cpp's own `#include`s stay confined to this
// header plus Onyx/Exchange/GltfExport.h itself.
SceneExportFn MakeGltfExportFn(bool embedBuffers, bool includeSkin);

} // namespace Onyx::Cli
