#include <doctest/doctest.h>

#include <OnyxBox/Module.h>
#include <OnyxBox/Writer.h>

#include <Onyx/Core/Cli/Resolve.h>
#include <Onyx/Core/Modules/Selection.h>
#include <Onyx/Core/Modules/Workspace.h>

#include <memory>
#include <sstream>
#include <string>

#include "Support/BoxFile.h"

using namespace Onyx;

namespace {

Domain::AssetEntry Named(std::string name, std::vector<Domain::AssetEntry> children = {}) {
    Domain::AssetEntry e;
    e.name = std::move(name);
    e.children = std::move(children);
    return e;
}

// @0 dir, @0/0 dup, @0/1 solo, @1 dup, @2 dup
struct Sample : Modules::Document {
    Sample() { roots = {Named("dir", {Named("dup"), Named("solo")}), Named("dup"), Named("dup")}; }
};

}

TEST_CASE("FindAllByName lists matches in depth-first order") {
    const Sample doc;

    const auto all = Modules::FindAllByName(doc, "dup");
    REQUIRE(all.size() == 3);
    CHECK(all[0].indices == std::vector<uint32_t>{0, 0});
    CHECK(all[1].indices == std::vector<uint32_t>{1});
    CHECK(all[2].indices == std::vector<uint32_t>{2});
    CHECK(Modules::FindAllByName(doc, "missing").empty());
    CHECK(Modules::FindByName(doc, "dup")->indices == all[0].indices);
}

TEST_CASE("ResolveEntry reports Found, NotFound, Ambiguous and BadPath") {
    OnyxBox::Writer writer;
    writer.AddText("dup", "first").AddText("solo", "only").AddText("dup", "second");
    const Tests::BoxFile file("resolve.obx", writer.Build());
    Modules::Workspace ws;
    REQUIRE(ws.AddModule(std::make_unique<OnyxBox::Module>()).ok());
    const Modules::DocumentId id = ws.Open(file.path);
    REQUIRE(id != 0);
    const Modules::Document& doc = *ws.Get(id);

    const Cli::Resolution solo = Cli::ResolveEntry(doc, "solo", false);
    CHECK(solo.status == Cli::ResolveStatus::Found);
    CHECK(solo.entry == &doc.roots[1]);
    CHECK(solo.matches.empty());

    const Cli::Resolution path = Cli::ResolveEntry(doc, "@2", false);
    CHECK(path.status == Cli::ResolveStatus::Found);
    CHECK(path.entry == &doc.roots[2]);

    const Cli::Resolution missing = Cli::ResolveEntry(doc, "missing", false);
    CHECK(missing.status == Cli::ResolveStatus::NotFound);
    CHECK(missing.entry == nullptr);
    CHECK(Cli::ResolveEntry(doc, "@9", false).status == Cli::ResolveStatus::NotFound);

    const Cli::Resolution ambiguous = Cli::ResolveEntry(doc, "dup", false);
    CHECK(ambiguous.status == Cli::ResolveStatus::Ambiguous);
    CHECK(ambiguous.entry == nullptr);
    CHECK(ambiguous.matches.size() == 2);

    const Cli::Resolution first = Cli::ResolveEntry(doc, "dup", true);
    CHECK(first.status == Cli::ResolveStatus::Found);
    CHECK(first.entry == &doc.roots[0]);

    const Cli::Resolution bad = Cli::ResolveEntry(doc, "@x", false);
    CHECK(bad.status == Cli::ResolveStatus::BadPath);
    CHECK(bad.entry == nullptr);
}

TEST_CASE("ResolveEntry takes the first depth-first match across nesting") {
    const Sample doc;

    const Cli::Resolution first = Cli::ResolveEntry(doc, "dup", true);
    CHECK(first.status == Cli::ResolveStatus::Found);
    CHECK(first.entry == &doc.roots[0].children[0]);
    CHECK(Cli::ResolveEntry(doc, "@0/1", false).entry == &doc.roots[0].children[1]);
}

TEST_CASE("ReportResolution prints the ambiguous line byte for byte") {
    const Sample doc;
    const Cli::Resolution r = Cli::ResolveEntry(doc, "dup", false);
    REQUIRE(r.status == Cli::ResolveStatus::Ambiguous);

    std::ostringstream out, err;
    CHECK_FALSE(Cli::ReportResolution(r, "dup", out, err));
    CHECK(err.str() == "cli.ambiguous: 3 entries are named 'dup': @0/0, @1, @2\n");
    CHECK(out.str().empty());
}

TEST_CASE("ReportResolution names a bad path on err and an unknown entry on out") {
    const Sample doc;
    std::ostringstream out, err;

    CHECK_FALSE(Cli::ReportResolution(Cli::ResolveEntry(doc, "@0/x", false), "@0/x", out, err));
    CHECK(err.str() == "invalid node path: @0/x\n");
    CHECK(out.str().empty());

    std::ostringstream out2, err2;
    CHECK_FALSE(Cli::ReportResolution(Cli::ResolveEntry(doc, "missing", false), "missing", out2, err2));
    CHECK(out2.str() == "unknown entry: missing\n");
    CHECK(err2.str().empty());

    std::ostringstream out3, err3;
    CHECK(Cli::ReportResolution(Cli::ResolveEntry(doc, "solo", false), "solo", out3, err3));
    CHECK(out3.str().empty());
    CHECK(err3.str().empty());
}
