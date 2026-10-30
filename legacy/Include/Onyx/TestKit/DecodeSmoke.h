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
// Four outcomes per entry:
//   - the entry is flagged Domain::NodeFlags::Failed (a PARSE-time
//     failure, e.g. a declared payload range beyond EOF)  -> skippedFailed,
//     decode is never attempted. This mirrors Source/App/ViewerOpening.cpp's
//     OpenSelection, which refuses to decode a Failed node outright (same
//     rule, same reasoning): a Failed node's parser already reported the
//     defect as a diag, so attempting to decode it anyway and recording a
//     SECOND failure would double-count the exact same root cause under two
//     different names. A toolkit author who fixes the parser-side defect
//     fixes it once, not once per gate that independently rediscovers it.
//   - no decoder registered for its type              -> skipped
//   - a decoder exists and returns a value             -> decoded
//   - a decoder exists but salvage-fails (returns
//     null -- e.g. a lying declared size, see
//     Tests/cli_test.cpp's WriteLyingImageBox)          -> failed, unless
//                                                           the entry's name
//                                                           is in
//                                                           `allowlist`, in
//                                                           which case it
//                                                           counts as
//                                                           skipped instead
//                                                           (a known,
//                                                           accepted
//                                                           failure, spec
//                                                           §10's "recorded
//                                                           allowlist")
//
// A branch/container entry (no decode capability registered for its own
// type, e.g. a folder node) is `skipped`, not an error -- the walk always
// recurses into children regardless of whether the parent itself decoded
// (and regardless of whether the parent was Failed -- a Failed parent's own
// children are still walked and may well be fine).

#include <Onyx/Modules/Workspace.h>

#include <string>
#include <vector>

namespace Onyx::TestKit {

struct SmokeResult {
    int decoded       = 0;
    int skipped       = 0;   // no decoder registered for this entry's type
    int skippedFailed = 0;   // entry was flagged NodeFlags::Failed at parse time; never attempted
    int failed        = 0;   // a decoder existed but salvage-failed (and wasn't allowlisted)
    std::vector<std::string> errors;   // one line per `failed` entry, entry name included
};

/// Walks `id`'s document tree (must already be open and ready --
/// ws.Get(id)->ready) and attempts to decode every entry via ws.Decoders(),
/// skipping any entry already flagged Domain::NodeFlags::Failed (counted in
/// `skippedFailed`, never in `failed` -- see this header's top comment for
/// why a parse-time failure is not re-derived as a decode failure).
/// `allowlist` names entries whose decode failure is expected and should
/// not count toward `failed` either. Returns a zeroed SmokeResult (no
/// entries touched) if `id` names no open document.
SmokeResult DecodeAll(Modules::Workspace& ws, Modules::DocumentId id,
                       const std::vector<std::string>& allowlist = {});

} // namespace Onyx::TestKit
