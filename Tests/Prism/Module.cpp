#include <doctest/doctest.h>

#include <Prism/Module.h>
#include <Prism/Writer.h>

#include "Support/Harness.h"
#include "Support/PrismFixture.h"

#include <Onyx/Core/Modules/Workspace.h>

#include <Prism/Palette.h>

#include <memory>
#include <vector>

using namespace Onyx;
using Tests::Find;
using Tests::HasDiag;
using Tests::Bytes;
using Tests::NewWorkspace;
using Tests::RawPrism;
using Tests::RawSection;
using Tests::SamplePrism;
using Tests::TempFile;

TEST_CASE("Probe accepts the magic and refuses other bytes") {
    Prism::Module module;
    const std::vector<std::byte> magic = {std::byte{'P'}, std::byte{'R'}, std::byte{'M'}, std::byte{'1'}};
    Modules::ProbeInput input;
    input.path = "thing.prm";
    input.header = magic;
    CHECK(module.Probe(input).confidence == 95);

    const std::vector<std::byte> other = {std::byte{'O'}, std::byte{'B'}, std::byte{'X'}, std::byte{'1'}};
    input.header = other;
    CHECK(module.Probe(input).confidence == 0);
    input.header = {};
    CHECK(module.Probe(input).confidence == 0);

    const Modules::ModuleInfo info = module.Info();
    CHECK(info.id == "prm");
    CHECK(info.displayName == "Prism");
    REQUIRE(info.hints.size() == 1);
    CHECK(info.hints[0] == "prism");
    REQUIRE(info.openFilters.size() == 1);
    CHECK(info.openFilters[0].extensions == std::vector<std::string>{"prm"});
}

TEST_CASE("ParseContainer lists every section in order with its kind and type key") {
    TempFile file("order.prm", SamplePrism());
    auto ws = NewWorkspace();

    const Modules::ProbeRanking rank = ws->Probe(file.path);
    REQUIRE(rank.winner != nullptr);
    CHECK(rank.rows[0].result.confidence == 95);

    Modules::Document* doc = ws->Get(ws->Open(file.path));
    REQUIRE(doc);
    REQUIRE(doc->roots.size() == 7);

    struct Row {
        const char* name;
        const char* key;
        Domain::MediaKind kind;
    };
    const Row expected[] = {
        {"palette", "prm.palette", Domain::MediaKind::Table},   {"tiles", "prm.image", Domain::MediaKind::Image},
        {"manifest", "prm.manifest", Domain::MediaKind::Text},  {"clip", "prm.clip", Domain::MediaKind::Audio},
        {"entries", "prm.entries", Domain::MediaKind::Table},   {"stats", "prm.stats", Domain::MediaKind::Raw},
        {"bad", "prm.image", Domain::MediaKind::Image},
    };
    for (size_t i = 0; i < doc->roots.size(); ++i) {
        CHECK(doc->roots[i].name == expected[i].name);
        CHECK(ws->Catalog().Key(doc->roots[i].type) == expected[i].key);
        CHECK(doc->roots[i].kind == expected[i].kind);
        CHECK(ws->Catalog().Media(doc->roots[i].type) == expected[i].kind);
    }
}

TEST_CASE("an out-of-range section is Failed with a diagnostic and the rest survives") {
    TempFile file("range.prm", SamplePrism());
    auto ws = NewWorkspace();
    Modules::Document* doc = ws->Get(ws->Open(file.path));
    REQUIRE(doc);

    const Domain::AssetEntry* bad = Find(*doc, "bad");
    REQUIRE(bad);
    CHECK(Domain::HasFlag(bad->flags, Domain::NodeFlags::Failed));
    CHECK(HasDiag(*doc, "prism.section.range", Services::Severity::Error));
    CHECK(doc->diags.Count(Services::Severity::Error) == 1);
    for (const Domain::AssetEntry& e : doc->roots) {
        if (e.name != "bad") CHECK_FALSE(Domain::HasFlag(e.flags, Domain::NodeFlags::Failed));
    }
}

TEST_CASE("an image and a stats record that do not match their sizes are Failed") {
    Prism::Writer w;
    w.SetPalette({{1, 2, 3, 4}});
    w.AddImage("liar", 4, 4, {0, 0, 0});
    w.AddStats("stats", 1, 1, 1.0f, 0);
    w.AddClip("clip", 8000, 1, {1, 2, 3});
    w.AddRange("shortStats", Prism::Kind::Stats, 0, 8);
    TempFile file("mismatch.prm", w.Build());
    auto ws = NewWorkspace();
    Modules::Document* doc = ws->Get(ws->Open(file.path));
    REQUIRE(doc);

    CHECK(Domain::HasFlag(Find(*doc, "liar")->flags, Domain::NodeFlags::Failed));
    CHECK(HasDiag(*doc, "prism.image.size", Services::Severity::Error));
    CHECK(Domain::HasFlag(Find(*doc, "shortStats")->flags, Domain::NodeFlags::Failed));
    CHECK(HasDiag(*doc, "prism.stats.size", Services::Severity::Error));
    CHECK_FALSE(Domain::HasFlag(Find(*doc, "stats")->flags, Domain::NodeFlags::Failed));
    CHECK_FALSE(Domain::HasFlag(Find(*doc, "clip")->flags, Domain::NodeFlags::Failed));
}

TEST_CASE("a file without a palette flags every image") {
    Prism::Writer w;
    w.AddImage("one", 1, 1, {0});
    w.AddImage("two", 1, 1, {0});
    w.AddManifest("manifest", "a = 1");
    TempFile file("nopalette.prm", w.Build());
    auto ws = NewWorkspace();
    Modules::Document* doc = ws->Get(ws->Open(file.path));
    REQUIRE(doc);

    CHECK(Domain::HasFlag(Find(*doc, "one")->flags, Domain::NodeFlags::Failed));
    CHECK(Domain::HasFlag(Find(*doc, "two")->flags, Domain::NodeFlags::Failed));
    CHECK_FALSE(Domain::HasFlag(Find(*doc, "manifest")->flags, Domain::NodeFlags::Failed));
    CHECK(HasDiag(*doc, "prism.noPalette", Services::Severity::Error));
    CHECK(doc->diags.Count(Services::Severity::Error) == 2);
}

TEST_CASE("a truncated section header stops the walk and keeps what was read") {
    std::vector<std::byte> bytes = SamplePrism();
    bytes.resize(10 + 11 + 7 + 2);
    TempFile file("truncated.prm", bytes);
    auto ws = NewWorkspace();
    Modules::Document* doc = ws->Get(ws->Open(file.path));
    REQUIRE(doc);
    CHECK(doc->roots.size() < 7);
    CHECK(HasDiag(*doc, "prism.section.truncated", Services::Severity::Warning));
}

TEST_CASE("a second palette is reported as prism.palette.duplicate and the first one is used") {
    Prism::Writer w;
    w.SetPalette({{10, 20, 30, 255}, {40, 50, 60, 255}});
    w.AddImage("pic", 1, 1, {1});
    w.AddRange("second", Prism::Kind::Palette, 0, 8);
    TempFile file("duplicate.prm", w.Build());
    auto ws = NewWorkspace();
    Modules::Document* doc = ws->Get(ws->Open(file.path));
    REQUIRE(doc);

    CHECK(HasDiag(*doc, "prism.palette.duplicate", Services::Severity::Error));
    CHECK(doc->diags.Count(Services::Severity::Error) == 1);
    CHECK(Domain::HasFlag(Find(*doc, "second")->flags, Domain::NodeFlags::Failed));
    CHECK_FALSE(Domain::HasFlag(Find(*doc, "palette")->flags, Domain::NodeFlags::Failed));
    CHECK_FALSE(Domain::HasFlag(Find(*doc, "pic")->flags, Domain::NodeFlags::Failed));

    const auto palette = std::static_pointer_cast<const Prism::Palette>(doc->moduleState);
    REQUIRE(palette);
    REQUIRE(palette->colors.size() == 2);
    CHECK(palette->colors[1] == glm::u8vec4(40, 50, 60, 255));
}

TEST_CASE("a file without the PRM1 magic reports prism.header.bad") {
    TempFile file("badmagic.prm", Bytes({'N', 'O', 'P', 'E', 0, 0, 0, 0, 0, 0}));
    auto ws = NewWorkspace();
    Modules::Document* doc = ws->Get(ws->Open(file.path, "prm"));
    REQUIRE(doc);
    CHECK(HasDiag(*doc, "prism.header.bad", Services::Severity::Error));
    CHECK(doc->roots.empty());
}

TEST_CASE("a file shorter than the header reports prism.header.truncated") {
    TempFile file("short.prm", Bytes({'P', 'R', 'M', '1', 1, 0}));
    auto ws = NewWorkspace();
    Modules::Document* doc = ws->Get(ws->Open(file.path, "prm"));
    REQUIRE(doc);
    CHECK(HasDiag(*doc, "prism.header.truncated", Services::Severity::Error));
    CHECK(doc->roots.empty());
}

TEST_CASE("a section count beyond what the file can hold is clamped with a warning") {
    TempFile file("clamped.prm", Bytes({'P', 'R', 'M', '1', 0, 0, 0xFF, 0xFF, 0xFF, 0x7F}));
    auto ws = NewWorkspace();
    Modules::Document* doc = ws->Get(ws->Open(file.path, "prm"));
    REQUIRE(doc);
    CHECK(HasDiag(*doc, "prism.sections.clamped", Services::Severity::Warning));
    CHECK(doc->roots.empty());
}

TEST_CASE("an unknown section kind is Failed with prism.section.kind") {
    Prism::Writer w;
    w.SetPalette({{1, 2, 3, 4}});
    w.AddRange("odd", static_cast<Prism::Kind>(9), 0, 4);
    TempFile file("kind.prm", w.Build());
    auto ws = NewWorkspace();
    Modules::Document* doc = ws->Get(ws->Open(file.path));
    REQUIRE(doc);
    CHECK(HasDiag(*doc, "prism.section.kind", Services::Severity::Error));
    CHECK(Domain::HasFlag(Find(*doc, "odd")->flags, Domain::NodeFlags::Failed));
    CHECK_FALSE(Domain::HasFlag(Find(*doc, "palette")->flags, Domain::NodeFlags::Failed));
}

TEST_CASE("a palette whose size is not paletteCount * 4 is Failed with prism.palette.size") {
    TempFile file("palsize.prm", RawPrism(3, {RawSection{0, "palette", Bytes({1, 2, 3, 4, 5, 6, 7, 8})}}));
    auto ws = NewWorkspace();
    Modules::Document* doc = ws->Get(ws->Open(file.path));
    REQUIRE(doc);
    CHECK(HasDiag(*doc, "prism.palette.size", Services::Severity::Error));
    CHECK(Domain::HasFlag(Find(*doc, "palette")->flags, Domain::NodeFlags::Failed));
}

TEST_CASE("a clip whose frame count does not match its payload is Failed with prism.clip.size") {
    // 8000 Hz, 1 channel, 5 frames declared, 2 frames present.
    const auto payload = Bytes({0x40, 0x1F, 0, 0, 1, 0, 5, 0, 0, 0, 1, 0, 2, 0});
    TempFile file("clipsize.prm", RawPrism(0, {RawSection{3, "clip", payload}}));
    auto ws = NewWorkspace();
    Modules::Document* doc = ws->Get(ws->Open(file.path));
    REQUIRE(doc);
    CHECK(HasDiag(*doc, "prism.clip.size", Services::Severity::Error));
    CHECK(Domain::HasFlag(Find(*doc, "clip")->flags, Domain::NodeFlags::Failed));
}
