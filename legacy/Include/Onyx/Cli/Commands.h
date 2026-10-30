#pragma once

// Generic headless CLI commands (M3a, spec Task 6). Every command operates
// against a Workspace that already has the caller's modules registered
// (AddModule) -- these functions know nothing about any specific game
// format, only the IGameModule/Workspace/DecoderRegistry contracts.
//
// Each of CmdProbe/CmdList/CmdExtract/CmdDecode is self-contained: it opens
// (and, for List/Extract/Decode, closes) its own Document, does its work,
// and drains the document's diags to `out` as plain
// "[severity] code: message" lines after its main output. CmdList's --json
// mode is the one exception: the whole JSON object IS the main output, so
// diags are folded into the object's own "diags" array instead of being
// re-printed as a trailing block of plain-text lines.

#include <Onyx/Modules/Workspace.h>
#include <Onyx/Parsers/SceneNode.h>

#include <array>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string>
#include <string_view>

namespace Onyx::Cli {

// `decode --to <format>` (T5, M5) exports a decoded Scene through a
// caller-supplied hook instead of a format-specific call Commands.cpp
// would have to link itself. Onyx_Core (Commands.cpp's own target)
// cannot link Onyx_Exchange the way it links nothing renderer-shaped
// either: Onyx_Exchange links Onyx_Core PUBLIC (root CMakeLists.txt), so
// the reverse link would be a real cycle -- the identical constraint
// Include/Onyx/Cli/Render.h documents at length for `render`/CmdRender.
// A composition root that DOES link both (Examples/OnyxCli/Main.cpp links
// Onyx::Exchange via Include/Onyx/Cli/Gltf.h's CmdDecodeGltf) supplies the
// hook; Commands.cpp only knows "a function from SceneData + an output
// path to success/failure", never Onyx::Exchange::GltfOptions or any
// other Exchange type by name.
using SceneExportFn = std::function<bool(const Parsers::SceneData&,
                                          const std::filesystem::path& out, std::string& err)>;

// `render` (M5 Task 6, spec §11) reaches the headless Vulkan renderer
// through the identical injected-hook shape SceneExportFn uses above:
// Commands.cpp/Run() compiles into Onyx_Core, which must not link
// Onyx_Render or Onyx_CliRender (Onyx_CliRender links Onyx_Core +
// Onyx_Render PUBLIC -- root CMakeLists.txt -- so the reverse link would
// be the same real cycle SceneExportFn's own comment explains). Unlike
// SceneExportFn, `Onyx::Cli::CmdRender` itself (Include/Onyx/Cli/Render.h)
// DOES ship in a linkable library (Onyx_CliRender) -- G1 in
// docs/design/2026-08-19-public-surface-audit.md -- so a composition root
// that links it can pass `Onyx::Cli::CmdRender` itself as this hook with
// no wrapper needed: RenderFn's parameter list is CmdRender's own
// (defaults stripped, since Run() always supplies every argument),
// so the function is directly assignable to a RenderFn without an
// adapter lambda. See Render.h's doc comment on CmdRender for what each
// parameter means; `view` must be one of kCanonicalViews below (CmdRender
// returns kUsage for anything else) and `strict` mirrors CmdDecode's own
// --strict contract (kStrictErrors when the document carries an Error
// diag, checked after the Scene decode that renders reads from).
using RenderFn = std::function<int(Modules::Workspace&, const std::filesystem::path& path,
                                    std::string_view entryName, const std::filesystem::path& outPng,
                                    int width, int height, std::ostream& out,
                                    std::string_view moduleHint, std::string_view view, bool strict)>;

// Canonical view set for `render --views a,b,c` (spec §11). Pure
// vocabulary shared between Run()'s argv parsing/--help text (Core-only,
// no renderer type in sight) and CmdRender's own camera lookup
// (Onyx_CliRender, Source/Cli/Render.cpp) -- CmdRender is the sole
// authority for what camera each name actually produces; this array just
// keeps the two in sync from one place instead of two string literals
// drifting apart. "iso" is also `render`'s default when --views is not
// given, and reproduces the exact 45deg-yaw/15deg-pitch orbit `render`
// used before --views existed (M4 Task 14), so a caller that never passes
// --views sees byte-identical output to before this flag was added.
inline constexpr std::array<std::string_view, 6> kCanonicalViews = {
    "iso", "front", "back", "left", "right", "top",
};

// Exit codes, shared by every command:
//   0 ok · 1 bad usage / entry not found / no decode capability ·
//   2 no module accepted the file (CmdList/CmdExtract/CmdDecode's Open
//   failed; also CmdProbe when the ranking has no winner) ·
//   3 --strict and the document has Error diags
//
// CmdDecode returns kOk whenever a decode capability existed for the
// entry, whether or not the decoder itself produced a value: a decoder
// returning null is a salvage failure the diags already explain (e.g. a
// lying declared size), not a usage error. kUsage is reserved for the
// two cases where the caller asked for something that doesn't exist:
// the entry itself, or any decode capability for its type.
inline constexpr int kOk = 0, kUsage = 1, kNoModule = 2, kStrictErrors = 3;

int CmdProbe (Modules::Workspace&, const std::filesystem::path&,
              std::ostream& out);                    // score/reason table;
              // kNoModule when the ranking has no winner
int CmdList  (Modules::Workspace&, const std::filesystem::path&,
              bool json, std::ostream& out,
              std::string_view moduleHint = {});     // tree; json = one object
int CmdExtract(Modules::Workspace&, const std::filesystem::path&,
              const std::filesystem::path& outDir, std::ostream& out,
              std::string_view moduleHint = {});
int CmdDecode(Modules::Workspace&, const std::filesystem::path&,
              std::string_view entryName, bool strict, std::ostream& out,
              std::string_view moduleHint = {},
              std::string_view toFormat = {}, const std::filesystem::path& toOut = {},
              const SceneExportFn& exportFn = {});
              // decodes by capability (image->PNG-less summary: type,
              // dimensions, byte count; text->prints it); emits diags.
              // `toFormat` non-empty requests an export instead of (or in
              // addition to) the usual summary line: the entry must have
              // Scene decode capability, `toFormat` must be a format this
              // CLI recognizes ("gltf" as of v1 -- spec §9 v1.1 widens
              // this), and `exportFn` must be non-null (the caller's
              // composition root wires it; see SceneExportFn's own doc
              // comment above) -- any of those missing is kUsage, same as
              // "unknown entry" or "no decode capability". A non-null
              // exportFn that itself returns false is also kUsage, with
              // its `err` string printed to `out`.

// argv dispatcher used by example/consumer mains -- `App::Run(argc, argv)`
// embeds this (spec §11), so every toolkit gets probe/list/extract/decode/
// render for free from linking Onyx_Core alone, whether or not it also
// links Onyx_CliRender/Onyx_Exchange (a caller with neither hook wired up
// still gets a *correct* kUsage for `render`/`decode --to`, not a missing
// verb -- see below). Accepts "--game <hint>" anywhere after the
// subcommand (any position); the hint is forwarded as moduleHint to
// Workspace::Open for list/extract/decode/render, where it wins outright
// over probe ranking (see Workspace::PrepareDocument). Also accepts "--to
// <format>" and "--out <path>" (decode only -- see CmdDecode's own doc
// comment above), forwarding `exportFn` as CmdDecode's exportFn when both
// are present; a caller with nothing to export from simply never passes
// exportFn (default {}), and "decode ... --to ..." then reports kUsage
// exactly as if no exporter existed, which is true.
//
// `render <file> <entryName> --out <path.png> [--width N] [--height N]
// [--views a,b,c] [--strict] [--game <hint>]` (M5 Task 6, spec §11):
// parsed here, dispatched through `renderFn` (RenderFn's doc comment
// above explains why this stays a hook rather than a direct CmdRender
// call). Without --views, exactly one render happens, straight to --out's
// path, with the default "iso" view -- byte-identical to `render`'s
// pre-Task-6 behavior (see kCanonicalViews' doc comment). With --views,
// one render happens per name (kCanonicalViews order is not enforced --
// callers get exactly the views they asked for, in the order given,
// duplicates included), each written beside --out's path with the view
// name inserted before the extension (`--out out.png --views iso,top`
// writes `out.iso.png` and `out.top.png`, never `out.png` itself). Any
// render call returning 77 (no Vulkan-capable device/driver -- Render.h's
// own doc comment) short-circuits the remaining views and returns 77
// immediately, matching the single-view case; a render call returning
// kUsage/kNoModule likewise stops immediately. kStrictErrors from one
// view does not stop the rest (every view decodes the same entry from the
// same document, so they would all report the same diags) -- Run()
// finishes every requested view and returns kStrictErrors once any of
// them did. `renderFn` unset (default {}) reports kUsage with a
// diagnostic explaining why, the same shape as `decode --to` with no
// exportFn.
int Run(Modules::Workspace&, int argc, char** argv,
        std::ostream& out, std::ostream& err,
        const SceneExportFn& exportFn = {}, const RenderFn& renderFn = {});

} // namespace Onyx::Cli
