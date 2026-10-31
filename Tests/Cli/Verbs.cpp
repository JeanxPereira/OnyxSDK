#include <doctest/doctest.h>

#include <Onyx/Cli/StockVerbs.h>
#include <Onyx/Core/Cli/Run.h>
#include <Onyx/Core/Cli/Verb.h>
#include <Onyx/Core/Modules/Workspace.h>

#include "Support/StubModule.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

using namespace Onyx;
using Onyx::Tests::StubEntry;
using Onyx::Tests::StubModule;
using Onyx::Tests::StubOptions;

namespace {

constexpr std::string_view kContainer = "hello boxABCDEFGH";

struct TempPath {
    std::filesystem::path path;
    explicit TempPath(const std::string& name) : path(std::filesystem::temp_directory_path() / ("onyx_cli_" + name)) {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    ~TempPath() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

struct TempFile : TempPath {
    explicit TempFile(const std::string& name, std::string_view contents = kContainer) : TempPath(name) {
        std::ofstream f(path, std::ios::binary);
        f << contents;
    }
};

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

StubEntry Entry(std::string name, std::string type, uint64_t offset, uint64_t size) {
    StubEntry e;
    e.name = std::move(name);
    e.type = std::move(type);
    e.offset = offset;
    e.size = size;
    return e;
}

StubOptions SampleOptions() {
    StubOptions options;
    options.script = {Entry("txt", "text", 0, 9), Entry("img", "image", 9, 8), Entry("bad", "blob", 0, 0xFFFFFFFFull)};
    options.script[2].failed = true;
    options.reportParseError = true;
    return options;
}

struct Harness {
    Modules::Workspace ws;
    Cli::VerbTable table;

    Harness() { table.Add(Cli::StockVerbs()); }

    struct Outcome {
        int rc = 0;
        std::string out;
        std::string err;
    };

    Outcome Invoke(std::vector<std::string> args) {
        args.insert(args.begin(), "onyx-test");
        std::vector<char*> argv;
        for (std::string& a : args) argv.push_back(a.data());
        std::ostringstream out, err;
        const int rc = Cli::Run(ws, table, static_cast<int>(argv.size()), argv.data(), out, err);
        return {rc, out.str(), err.str()};
    }
};

bool Contains(const std::string& text, std::string_view needle) {
    return text.find(needle) != std::string::npos;
}

}

TEST_CASE("Cli.ProbePrintsTableWithRowsAndWinner") {
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(SampleOptions()));
    TempFile file("probe.bin");

    const auto r = h.Invoke({"probe", file.path.string()});
    CHECK(r.rc == Cli::kOk);
    CHECK(Contains(r.out, "stub"));
    CHECK(Contains(r.out, "90"));
    CHECK(Contains(r.out, "winner: stub"));
}

TEST_CASE("Cli.ProbeReturnsKNoModuleWhenTheRankingHasNoWinner") {
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(StubOptions{.probeScore = 0}));
    TempFile file("probe_none.bin");

    const auto r = h.Invoke({"probe", file.path.string()});
    CHECK(r.rc == Cli::kNoModule);
    CHECK(Contains(r.out, "winner: none"));
}

TEST_CASE("Cli.ListJsonContainsTypeKeyAndFailedFlag") {
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(SampleOptions()));
    TempFile file("list.bin");

    const auto r = h.Invoke({"list", file.path.string(), "--json"});
    CHECK(r.rc == Cli::kOk);
    CHECK(Contains(r.out, "\"type\":\"stub.image\""));
    CHECK(Contains(r.out, "\"failed\":true"));
    CHECK(Contains(r.out, "\"path\":"));
    CHECK(Contains(r.out, "\"module\":\"stub\""));
    CHECK(Contains(r.out, "stub.parse.bad"));
}

TEST_CASE("Cli.ListPrintsTree") {
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(SampleOptions()));
    TempFile file("list_text.bin");

    const auto r = h.Invoke({"list", file.path.string()});
    CHECK(r.rc == Cli::kOk);
    CHECK(Contains(r.out, "txt  stub.text  9 bytes"));
    CHECK(Contains(r.out, "bad  stub.blob  4294967295 bytes [FAILED]"));
}

TEST_CASE("Cli.ListReturnsKNoModuleForAFileNoModuleAccepts") {
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(StubOptions{.probeScore = 0}));
    TempFile file("list_none.bin");

    CHECK(h.Invoke({"list", file.path.string()}).rc == Cli::kNoModule);
}

TEST_CASE("Cli.ExtractWritesPayloadBytesAndSkipsFailedEntries") {
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(SampleOptions()));
    TempFile file("extract.bin");
    TempPath dir("extract_out");

    const auto r = h.Invoke({"extract", file.path.string(), dir.path.string()});
    CHECK(r.rc == Cli::kOk);
    CHECK(ReadFile(dir.path / "txt") == "hello box");
    CHECK(ReadFile(dir.path / "img") == "ABCDEFGH");
    CHECK_FALSE(std::filesystem::exists(dir.path / "bad"));
}

TEST_CASE("Cli.ExtractReportsDuplicateLeafNames") {
    StubOptions options;
    options.script = {Entry("dup", "text", 0, 5), Entry("dup", "text", 5, 4), Entry("other", "text", 0, 5)};
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(options));
    TempFile file("extract_dup.bin");
    TempPath dir("extract_dup_out");

    const auto r = h.Invoke({"extract", file.path.string(), dir.path.string()});
    CHECK(r.rc == Cli::kPartial);
    CHECK(Contains(r.out, "skipped 'dup': duplicate name"));
    CHECK(ReadFile(dir.path / "dup") == "hello");
    CHECK(ReadFile(dir.path / "other") == "hello");
}

TEST_CASE("Cli.ExtractSkipsUnsafeNamesAndWritesNothingOutsideTheDir") {
    StubOptions options;
    for (const char* name : {"../evil.txt", "a/b.txt", "NUL", "con.txt", "good.txt"}) {
        options.script.push_back(Entry(name, "blob", 0, 9));
    }
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(options));
    TempFile file("unsafe.bin");
    TempPath dir("unsafe_out");

    const auto r = h.Invoke({"extract", file.path.string(), dir.path.string()});
    CHECK(r.rc == Cli::kPartial);
    CHECK(Contains(r.out, "skipped '../evil.txt': unsafe name"));
    CHECK(Contains(r.out, "skipped 'a/b.txt': unsafe name"));
    CHECK(Contains(r.out, "skipped 'NUL': unsafe name"));
    CHECK(Contains(r.out, "skipped 'con.txt': unsafe name"));

    REQUIRE(std::filesystem::exists(dir.path / "good.txt"));
    CHECK_FALSE(std::filesystem::exists(dir.path.parent_path() / "evil.txt"));
    CHECK_FALSE(std::filesystem::exists(dir.path / "a"));
    size_t count = 0;
    for (const auto& item : std::filesystem::directory_iterator(dir.path)) {
        (void)item;
        ++count;
    }
    CHECK(count == 1);
}

TEST_CASE("Cli.ExtractResolvesBytesFromAModuleRegisteredSecondFile") {
    StubOptions options;
    options.extraFiles = {"junk-THE REAL BYTES-junk"};
    StubEntry e = Entry("payload.bin", "blob", 5, 14);
    e.fileIndex = 1;
    options.script = {e};
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(options));
    TempFile file("synth.bin", std::string(36, '\xFF'));
    TempPath dir("synth_out");

    const auto r = h.Invoke({"extract", file.path.string(), dir.path.string()});
    CHECK(r.rc == Cli::kOk);
    CHECK(ReadFile(dir.path / "1" / "payload.bin") == "THE REAL BYTES");
}

TEST_CASE("Cli.ExtractSalvagesAnEntryWhoseFileIndexWasNeverRegistered") {
    StubOptions options;
    StubEntry ghost = Entry("ghost.bin", "blob", 0, 4);
    ghost.fileIndex = 99;
    options.script = {ghost, Entry("txt", "text", 0, 9)};
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(options));
    TempFile file("bogus.bin");
    TempPath dir("bogus_out");

    const auto r = h.Invoke({"extract", file.path.string(), dir.path.string()});
    CHECK_FALSE(std::filesystem::exists(dir.path / "ghost.bin"));
    CHECK(Contains(r.out, "ghost.bin"));
    CHECK(Contains(r.out, "file index 99"));
    CHECK(Contains(r.out, "out of range"));
    CHECK(ReadFile(dir.path / "txt") == "hello box");
}

TEST_CASE("Cli.ExtractReturnsKPartialWhenAnEntryFails") {
    StubOptions options;
    StubEntry ghost = Entry("ghost.bin", "blob", 0, 4);
    ghost.fileIndex = 99;
    options.script = {Entry("txt", "text", 0, 9), ghost};
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(options));
    TempFile file("partial.bin");
    TempPath dir("partial_out");

    CHECK(h.Invoke({"extract", file.path.string(), dir.path.string()}).rc == Cli::kPartial);
}

TEST_CASE("Cli.ExtractDoesNotAllocateDeclaredSizeBeyondFile") {
    StubOptions options;
    options.script = {Entry("huge.bin", "blob", 0, 0xFFFFFFFFull)};
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(options));
    TempFile file("huge.bin");
    TempPath dir("huge_out");

    const auto start = std::chrono::steady_clock::now();
    const auto r = h.Invoke({"extract", file.path.string(), dir.path.string()});
    const auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(elapsed < std::chrono::seconds(1));
    CHECK(ReadFile(dir.path / "huge.bin") == kContainer);
    CHECK(r.rc == Cli::kPartial);
    CHECK(Contains(r.out, "truncated"));
}

TEST_CASE("Cli.DecodeTextEntryPrintsContentAndReturnsKOk") {
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(SampleOptions()));
    TempFile file("decode_text.bin");

    const auto r = h.Invoke({"decode", file.path.string(), "txt"});
    CHECK(r.rc == Cli::kOk);
    CHECK(Contains(r.out, "text txt: hello box"));
}

TEST_CASE("Cli.DecodeStrictReturnsKStrictErrorsWhenTheDocumentCarriesAnErrorDiag") {
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(SampleOptions()));
    TempFile file("decode_strict.bin");

    CHECK(h.Invoke({"decode", file.path.string(), "txt", "--strict"}).rc == Cli::kStrictErrors);
    CHECK(h.Invoke({"decode", file.path.string(), "txt"}).rc == Cli::kOk);
}

TEST_CASE("Cli.DecodeSummarisesEveryCapability") {
    StubOptions options;
    options.script = {Entry("mesh", "scene", 0, 0), Entry("pic", "image", 0, 0), Entry("snd", "audio", 0, 0),
                      Entry("tbl", "table", 0, 0), Entry("hdr", "schema", 0, 1), Entry("thing", "any", 0, 0)};
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(options));
    TempFile file("decode_all.bin");
    const std::string path = file.path.string();

    CHECK(Contains(h.Invoke({"decode", path, "mesh"}).out, "scene mesh parts=1 materials=1 vertices=4 triangles=2"));
    CHECK(Contains(h.Invoke({"decode", path, "pic"}).out, "image pic 2x3 rgba8"));
    CHECK(Contains(h.Invoke({"decode", path, "snd"}).out, "audio snd rate=8000 channels=2 seconds=1.00"));
    CHECK(Contains(h.Invoke({"decode", path, "tbl"}).out, "table tbl columns=2 rows=3"));
    CHECK(Contains(h.Invoke({"decode", path, "hdr"}).out, "schema hdr:"));
    CHECK(Contains(h.Invoke({"decode", path, "hdr"}).out, "version"));
    CHECK(Contains(h.Invoke({"decode", path, "thing"}).out, "any thing: thing"));
}

TEST_CASE("Cli.DecodeUnknownEntryReturnsKUsage") {
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(SampleOptions()));
    TempFile file("decode_unknown.bin");

    const auto r = h.Invoke({"decode", file.path.string(), "does-not-exist"});
    CHECK(r.rc == Cli::kUsage);
    CHECK(Contains(r.out, "unknown entry"));
}

TEST_CASE("Cli.DecodeEntryWithoutDecoderReturnsKUsage") {
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(SampleOptions()));
    TempFile file("decode_nodecoder.bin");

    const auto r = h.Invoke({"decode", file.path.string(), "bad"});
    CHECK(r.rc == Cli::kUsage);
    CHECK(Contains(r.out, "no decoder for entry"));
}

TEST_CASE("Cli.DecodeReturnsKOkWhenACapabilityExistsButTheDecoderSalvageFails") {
    StubOptions options;
    options.script = {Entry("liar", "badimage", 0, 4)};
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(options));
    TempFile file("decode_liar.bin");

    const auto r = h.Invoke({"decode", file.path.string(), "liar"});
    CHECK(r.rc == Cli::kOk);
    CHECK(Contains(r.out, "stub.image.size-mismatch"));
    CHECK_FALSE(Contains(r.out, "image liar"));
}

TEST_CASE("Cli.DecodeResolvesNodePathSyntax") {
    StubOptions options;
    StubEntry dir = Entry("dir", "blob", 0, 0);
    StubEntry nested = Entry("nested", "blob", 0, 0);
    nested.children = {Entry("a", "blob", 0, 0), Entry("leaf", "text", 0, 9)};
    dir.children = {Entry("first", "blob", 0, 0), nested};
    options.script = {dir};
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(options));
    TempFile file("decode_path.bin");
    const std::string path = file.path.string();

    const auto ok = h.Invoke({"decode", path, "@0/1/1"});
    CHECK(ok.rc == Cli::kOk);
    CHECK(Contains(ok.out, "text leaf: hello box"));

    CHECK(h.Invoke({"decode", path, "@0/1/7"}).rc == Cli::kUsage);
    CHECK(h.Invoke({"decode", path, "@0/x"}).rc == Cli::kUsage);
    CHECK(h.Invoke({"decode", path, "@"}).rc == Cli::kUsage);
}

TEST_CASE("Cli.DecodeWithADuplicatedNameAndNoFirstExitsKUsageNamingThePaths") {
    StubOptions options;
    options.script = {Entry("txt", "text", 0, 9), Entry("img", "image", 9, 8), Entry("img", "image", 9, 8)};
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(options));
    TempFile file("decode_dup.bin");

    const auto r = h.Invoke({"decode", file.path.string(), "img"});
    CHECK(r.rc == Cli::kUsage);
    CHECK(Contains(r.err, "cli.ambiguous: 2 entries are named 'img': @1, @2"));
    CHECK_FALSE(Contains(r.out, "image img"));
}

TEST_CASE("Cli.DecodeFirstTakesTheEarliestMatch") {
    StubOptions options;
    options.script = {Entry("dup", "text", 0, 5), Entry("dup", "text", 5, 4)};
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(options));
    TempFile file("decode_first.bin");

    const auto r = h.Invoke({"decode", file.path.string(), "dup", "--first"});
    CHECK(r.rc == Cli::kOk);
    CHECK(Contains(r.out, "text dup: hello"));
    CHECK(r.err.empty());
}

TEST_CASE("Cli.DecodeAmbiguousNamesInsideDirectoriesListEveryPath") {
    StubOptions options;
    StubEntry dir = Entry("dir", "blob", 0, 0);
    dir.children = {Entry("dup", "text", 0, 5), Entry("other", "blob", 0, 0)};
    options.script = {dir, Entry("dup", "text", 5, 4)};
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(options));
    TempFile file("decode_dup_nested.bin");

    const auto r = h.Invoke({"decode", file.path.string(), "dup"});
    CHECK(r.rc == Cli::kUsage);
    CHECK(Contains(r.err, "@0/0, @1"));
    CHECK(h.Invoke({"decode", file.path.string(), "@1"}).rc == Cli::kOk);
}

TEST_CASE("Cli.ExtractAcceptsFirstAndStillReportsDuplicateLeafNames") {
    StubOptions options;
    options.script = {Entry("dup", "text", 0, 5), Entry("dup", "text", 5, 4)};
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(options));
    TempFile file("extract_first.bin");
    TempPath dir("extract_first_out");

    const auto r = h.Invoke({"extract", file.path.string(), dir.path.string(), "--first"});
    CHECK(r.rc == Cli::kPartial);
    CHECK(ReadFile(dir.path / "dup") == "hello");
}

TEST_CASE("Cli.DecodeToExportsTheSceneThroughTheRegisteredExporter") {
    StubOptions options;
    options.script = {Entry("mesh", "scene", 0, 0), Entry("txt", "text", 0, 9)};
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(options));
    TempFile file("decode_to.bin");
    TempPath outPath("decode_to.glb");
    size_t parts = 0;
    h.table.Exporters().Add("gltf", Domain::MediaKind::Mesh,
                            [&parts](const void* payload, const std::filesystem::path& out) {
                                parts = static_cast<const Domain::Scene*>(payload)->parts.size();
                                std::ofstream(out, std::ios::binary) << "glb";
                                return Result<void>::Ok();
                            });

    const auto r = h.Invoke({"decode", file.path.string(), "mesh", "--to", "gltf", "--out", outPath.path.string()});
    CHECK(r.rc == Cli::kOk);
    CHECK(Contains(r.out, "exported mesh to"));
    CHECK(parts == 1);
    CHECK(ReadFile(outPath.path) == "glb");

    CHECK(h.Invoke({"decode", file.path.string(), "txt", "--to", "gltf", "--out", outPath.path.string()}).rc ==
          Cli::kUsage);
    CHECK(h.Invoke({"decode", file.path.string(), "mesh", "--to", "gltf"}).rc == Cli::kUsage);
}

TEST_CASE("Cli.DecodeToExportWriteFailureIsPartial") {
    StubOptions options;
    options.script = {Entry("mesh", "scene", 0, 0)};
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(options));
    TempFile file("decode_to_fail.bin");
    TempPath outPath("decode_to_fail.glb");
    h.table.Exporters().Add("gltf", Domain::MediaKind::Mesh, [](const void*, const std::filesystem::path&) {
        return Result<void>::Fail({"export.io", "disk full", 0});
    });

    const auto r = h.Invoke({"decode", file.path.string(), "mesh", "--to", "gltf", "--out", outPath.path.string()});
    CHECK(r.rc == Cli::kPartial);
    CHECK(Contains(r.out, "export failed: disk full"));
}

TEST_CASE("Cli.DecodeToWithNoExporterRegisteredReturnsKUsage") {
    StubOptions options;
    options.script = {Entry("mesh", "scene", 0, 0)};
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(options));
    TempFile file("decode_to_none.bin");

    const auto r = h.Invoke({"decode", file.path.string(), "mesh", "--to", "gltf", "--out", "unused.glb"});
    CHECK(r.rc == Cli::kUsage);
    CHECK(Contains(r.out, "unknown export format"));
}

TEST_CASE("Cli.RunDispatchesProbeListExtractAndDecodeByName") {
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(SampleOptions()));
    TempFile file("dispatch.bin");
    TempPath dir("dispatch_out");
    const std::string path = file.path.string();

    CHECK(Contains(h.Invoke({"probe", path}).out, "winner"));
    CHECK(Contains(h.Invoke({"decode", path, "txt"}).out, "hello box"));
    CHECK(Contains(h.Invoke({"list", path, "--json"}).out, "\"type\":\"stub.image\""));
    CHECK(h.Invoke({"extract", path, dir.path.string()}).rc == Cli::kOk);
}

TEST_CASE("Cli.GameHintOverridesRankingToPickTheCorrectModule") {
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(StubOptions{.id = "decoy", .probeScore = 99, .hints = {"decoy"}}));
    h.ws.AddModule(std::make_unique<StubModule>(SampleOptions()));
    TempFile file("hint.bin");
    const std::string path = file.path.string();

    const auto ranked = h.Invoke({"list", path, "--json"});
    CHECK(Contains(ranked.out, "\"module\":\"decoy\""));
    CHECK(Contains(ranked.out, "\"entries\":[]"));

    const auto hinted = h.Invoke({"list", path, "--json", "--game", "stub"});
    CHECK(Contains(hinted.out, "\"module\":\"stub\""));
    CHECK(Contains(hinted.out, "\"type\":\"stub.image\""));

    CHECK(Contains(h.Invoke({"probe", path, "--game", "stub"}).out, "winner: stub"));
}

TEST_CASE("Cli.RunReturnsKUsageToErrForAJunkCommand") {
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(SampleOptions()));

    const auto r = h.Invoke({"frobnicate"});
    CHECK(r.rc == Cli::kUsage);
    CHECK_FALSE(r.err.empty());
    CHECK(r.out.empty());
    CHECK(Contains(r.err, "decode"));
    CHECK(Contains(r.err, "extract"));
}

TEST_CASE("Cli.VerbsRejectMissingArguments") {
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(SampleOptions()));

    for (const char* verb : {"probe", "list", "extract", "decode"}) {
        const auto r = h.Invoke({verb});
        CHECK(r.rc == Cli::kUsage);
        CHECK(Contains(r.err, "usage:"));
    }
}

TEST_CASE("Cli.UnknownFlagReturnsKUsage") {
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(SampleOptions()));
    TempFile file("flag_unknown.bin");
    TempPath dir("flag_unknown_out");

    const auto r = h.Invoke({"extract", file.path.string(), dir.path.string(), "--bogus"});
    CHECK(r.rc == Cli::kUsage);
    CHECK(Contains(r.err, "usage: extract"));
    CHECK_FALSE(std::filesystem::exists(dir.path));
}

TEST_CASE("Cli.MistypedJsonFlagReturnsKUsage") {
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(SampleOptions()));
    TempFile file("flag_jsno.bin");

    const auto r = h.Invoke({"list", file.path.string(), "--jsno"});
    CHECK(r.rc == Cli::kUsage);
    CHECK(Contains(r.err, "usage: list"));
    CHECK(r.out.empty());
}

TEST_CASE("Cli.ValueFlagWithoutValueReturnsKUsage") {
    Harness h;
    h.ws.AddModule(std::make_unique<StubModule>(SampleOptions()));

    const auto r = h.Invoke({"decode", "x", "y", "--to"});
    CHECK(r.rc == Cli::kUsage);
    CHECK(Contains(r.err, "usage: decode"));
    CHECK(h.Invoke({"probe", "x", "--game"}).rc == Cli::kUsage);
}
