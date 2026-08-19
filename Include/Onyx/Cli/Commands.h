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

// argv dispatcher used by example/consumer mains. Accepts "--game <hint>"
// anywhere after the subcommand (any position); the hint is forwarded as
// moduleHint to Workspace::Open for list/extract/decode, where it wins
// outright over probe ranking (see Workspace::PrepareDocument). Also
// accepts "--to <format>" and "--out <path>" (decode only -- see
// CmdDecode's own doc comment above), forwarding `exportFn` as CmdDecode's
// exportFn when both are present; a caller with nothing to export from
// simply never passes exportFn (default {}), and "decode ... --to ..."
// then reports kUsage exactly as if no exporter existed, which is true.
int Run(Modules::Workspace&, int argc, char** argv,
        std::ostream& out, std::ostream& err,
        const SceneExportFn& exportFn = {});

} // namespace Onyx::Cli
