#include <doctest/doctest.h>

#include <Onyx/Core/Cli/Resolve.h>
#include <Onyx/Core/Modules/Selection.h>

#include <sstream>
#include <string>

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

TEST_CASE("CliResolve.FindAllByNameListsEveryMatchInDepthFirstOrder") {
    const Sample doc;

    const auto all = Modules::FindAllByName(doc, "dup");
    REQUIRE(all.size() == 3);
    CHECK(all[0].indices == std::vector<uint32_t>{0, 0});
    CHECK(all[1].indices == std::vector<uint32_t>{1});
    CHECK(all[2].indices == std::vector<uint32_t>{2});
    CHECK(Modules::FindAllByName(doc, "missing").empty());
    CHECK(Modules::FindByName(doc, "dup")->indices == all[0].indices);
}

TEST_CASE("CliResolve.PathResolvesDirectly") {
    const Sample doc;
    std::ostringstream err;

    const auto* entry = Cli::ResolveEntry(doc, "@0/1", false, err);
    REQUIRE(entry != nullptr);
    CHECK(entry->name == "solo");
    CHECK(err.str().empty());
}

TEST_CASE("CliResolve.MalformedPathNamesItOnErr") {
    const Sample doc;
    std::ostringstream err;

    CHECK(Cli::ResolveEntry(doc, "@0/x", false, err) == nullptr);
    CHECK(err.str() == "invalid node path: @0/x\n");
    CHECK(Cli::ResolveEntry(doc, "@9", true, err) == nullptr);
}

TEST_CASE("CliResolve.SingleMatchResolvesWithoutFirst") {
    const Sample doc;
    std::ostringstream err;

    const auto* entry = Cli::ResolveEntry(doc, "solo", false, err);
    REQUIRE(entry != nullptr);
    CHECK(entry->name == "solo");
    CHECK(err.str().empty());
}

TEST_CASE("CliResolve.UnknownNameReturnsNullSilently") {
    const Sample doc;
    std::ostringstream err;

    CHECK(Cli::ResolveEntry(doc, "missing", true, err) == nullptr);
    CHECK(err.str().empty());
}

TEST_CASE("CliResolve.SeveralMatchesWithFirstTakeTheEarliest") {
    const Sample doc;
    std::ostringstream err;

    const auto* entry = Cli::ResolveEntry(doc, "dup", true, err);
    CHECK(entry == &doc.roots[0].children[0]);
    CHECK(err.str().empty());
}

TEST_CASE("CliResolve.SeveralMatchesWithoutFirstAreAmbiguous") {
    const Sample doc;
    std::ostringstream err;

    CHECK(Cli::ResolveEntry(doc, "dup", false, err) == nullptr);
    CHECK(err.str() == "cli.ambiguous: 3 entries are named 'dup': @0/0, @1, @2\n");
}
