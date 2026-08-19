#pragma once
// ── TestKit::DecodeSmoke (spec §10) ────────────────────────────────────────
//
// "Decode smoke -- walk a container, decode everything decodable, assert
// zero Error diags (or a recorded allowlist)." DecodeAll walks an already-
// open Document's tree (same recursive shape Source/Cli/Commands.cpp's
// CmdDecode already exercises one entry at a time, generalized here to
// every entry) and, for each one, tries the DecoderRegistry's Scene > Image
// > Text capability lookup in that same priority order (spec §11, "CLI and
// GUI routing must not diverge" -- DecodeAll is a third caller of that same
// contract, not a new one).
//
// Three outcomes per entry:
//   - no decoder registered for its type            -> skipped
//   - a decoder exists and returns a value           -> decoded
//   - a decoder exists but salvage-fails (returns
//     null -- e.g. a lying declared size, see
//     Tests/cli_test.cpp's WriteLyingImageBox)        -> failed, unless the
//                                                         entry's name is in
//                                                         `allowlist`, in
//                                                         which case it
//                                                         counts as skipped
//                                                         instead (a known,
//                                                         accepted failure,
//                                                         spec §10's
//                                                         "recorded
//                                                         allowlist")
//
// A branch/container entry (no decode capability registered for its own
// type, e.g. a folder node) is `skipped`, not an error -- the walk always
// recurses into children regardless of whether the parent itself decoded.

#include <Onyx/Modules/Workspace.h>

#include <string>
#include <vector>

namespace Onyx::TestKit {

struct SmokeResult {
    int decoded = 0;
    int skipped = 0;
    int failed  = 0;
    std::vector<std::string> errors;   // one line per failed entry, entry name included
};

/// Walks `id`'s document tree (must already be open and ready --
/// ws.Get(id)->ready) and attempts to decode every entry via
/// ws.Decoders(). `allowlist` names entries whose decode failure is
/// expected and should not count toward `failed` (see this header's top
/// comment). Returns a zeroed SmokeResult (no entries touched) if `id`
/// names no open document.
SmokeResult DecodeAll(Modules::Workspace& ws, Modules::DocumentId id,
                       const std::vector<std::string>& allowlist = {});

} // namespace Onyx::TestKit
