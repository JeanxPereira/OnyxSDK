#include <doctest/doctest.h>

#include <Onyx/App/InfoTabFilter.h>
#include <Onyx/Services/Diagnostics.h>

using namespace Onyx::App;
using namespace Onyx::Services;

TEST_CASE("InfoTabFilter: DiagMentionsEntry matches a message containing the entry name") {
    Diag d{Severity::Error, "gowr.mesh.lod-missing", "entry hero.mesh has no LOD 0", std::nullopt};
    CHECK(DiagMentionsEntry(d, "hero.mesh"));
}

TEST_CASE("InfoTabFilter: DiagMentionsEntry rejects a message that does not name the entry") {
    Diag d{Severity::Warning, "t.w", "unrelated warning", std::nullopt};
    CHECK_FALSE(DiagMentionsEntry(d, "hero.mesh"));
}

TEST_CASE("InfoTabFilter: DiagMentionsEntry rejects an empty entry name") {
    // An empty needle would otherwise substring-match every message.
    Diag d{Severity::Info, "t.i", "anything at all", std::nullopt};
    CHECK_FALSE(DiagMentionsEntry(d, ""));
}

TEST_CASE("InfoTabFilter: DiagMentionsEntry is a plain substring match, not a whole-word one") {
    // Honest about the over-match this file's header comment calls out.
    Diag d{Severity::Error, "t.e", "hero.meshX corrupt", std::nullopt};
    CHECK(DiagMentionsEntry(d, "hero.mesh"));
}
