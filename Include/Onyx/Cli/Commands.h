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
//   0 ok · 1 bad usage/file/entry not found · 2 no module accepted
//   3 --strict and the document has Error diags
inline constexpr int kOk = 0, kUsage = 1, kNoModule = 2, kStrictErrors = 3;

int CmdProbe (Modules::Workspace&, const std::filesystem::path&,
              std::ostream& out);                    // score/reason table
int CmdList  (Modules::Workspace&, const std::filesystem::path&,
              bool json, std::ostream& out);         // tree; json = one object
int CmdExtract(Modules::Workspace&, const std::filesystem::path&,
              const std::filesystem::path& outDir, std::ostream& out);
int CmdDecode(Modules::Workspace&, const std::filesystem::path&,
              std::string_view entryName, bool strict, std::ostream& out);
              // decodes by capability (image->PNG-less summary: type,
              // dimensions, byte count; text->prints it); emits diags

// argv dispatcher used by example/consumer mains:
int Run(Modules::Workspace&, int argc, char** argv,
        std::ostream& out, std::ostream& err);

} // namespace Onyx::Cli
