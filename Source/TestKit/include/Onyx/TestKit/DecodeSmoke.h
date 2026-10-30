#pragma once

#include <Onyx/Core/Modules/Workspace.h>

#include <string>
#include <vector>

namespace Onyx::TestKit {

struct SmokeResult {
    // Entries whose every reported capability decoded.
    int decoded = 0;
    // Entries with no decoder, plus allowlisted entries that failed.
    int skipped = 0;
    // Entries already flagged Failed at parse time. They are never handed to a decoder.
    int skippedFailed = 0;
    // Entries where at least one reported capability returned nothing.
    int failed = 0;
    // One line per failed entry naming the entry and the slots that failed.
    std::vector<std::string> errors;
};

// Walks the tree of an open document and exercises every slot that Capabilities reports for each
// entry: scene, image, text, audio, table, schema and each named any-capability. `allowlist` names
// entries whose failure is expected. An unknown document yields an empty result.
SmokeResult DecodeAll(Modules::Workspace& ws, Modules::DocumentId id, const std::vector<std::string>& allowlist = {});

}
