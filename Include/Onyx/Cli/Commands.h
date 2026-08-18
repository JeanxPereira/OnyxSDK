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

#include <filesystem>
#include <iosfwd>
#include <string_view>

namespace Onyx::Cli {

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
              std::string_view moduleHint = {});
              // decodes by capability (image->PNG-less summary: type,
              // dimensions, byte count; text->prints it); emits diags

// argv dispatcher used by example/consumer mains. Accepts "--game <hint>"
// anywhere after the subcommand (any position); the hint is forwarded as
// moduleHint to Workspace::Open for list/extract/decode, where it wins
// outright over probe ranking (see Workspace::PrepareDocument).
int Run(Modules::Workspace&, int argc, char** argv,
        std::ostream& out, std::ostream& err);

} // namespace Onyx::Cli
