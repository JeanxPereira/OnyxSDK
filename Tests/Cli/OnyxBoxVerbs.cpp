#include <doctest/doctest.h>

#include <OnyxBox/Module.h>
#include <OnyxBox/Writer.h>

#include "Support/BoxFixture.h"

#include <Onyx/Cli/StockVerbs.h>
#include <Onyx/Core/Cli/Run.h>
#include <Onyx/Core/Cli/Verb.h>
#include <Onyx/Core/Domain/ByteRange.h>
#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/Core/Services/Paths.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

using namespace Onyx;
using Onyx::Tests::SampleBox;

static_assert(sizeof(Domain::ByteRange) == 24, "ByteRange must stay a tight 24-byte value type");

namespace {

struct TempPath {
    std::filesystem::path path;

    explicit TempPath(const std::string& name) : path(std::filesystem::temp_directory_path() / ("onyx_obxcli_" + name)) {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    ~TempPath() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

struct TempFile : TempPath {
    TempFile(const std::string& name, const std::vector<std::byte>& bytes) : TempPath(name) {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
};

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

bool Contains(const std::string& text, std::string_view needle) {
    return text.find(needle) != std::string::npos;
}

std::vector<std::byte> UnsafeNamesBox() {
    OnyxBox::Writer w;
    w.AddText("good.txt", "safe data");
    w.AddText("../evil.txt", "evil data");
    w.AddText("a/b.txt", "nested data");
    w.AddText("NUL", "device data");
    w.AddText("con.txt", "device ext data");
    return w.Build();
}

std::vector<std::byte> LyingImageBox() {
    OnyxBox::Writer w;
    std::vector<std::byte> liar = {std::byte{8}, std::byte{0}, std::byte{8}, std::byte{0}};
    liar.insert(liar.end(), 6, std::byte{0xAA});
    w.AddEntry("liar", OnyxBox::Kind::Image, liar);
    w.AddText("safe", "SAFE_TEXT");
    w.AddBlob("padding", std::vector<std::byte>(512, std::byte{0xEE}));
    return w.Build();
}

std::vector<std::byte> HugeSizeBox() {
    OnyxBox::Writer w;
    w.AddRange("huge", OnyxBox::Kind::Blob, 20, 0xFFFFFFFFu);
    return w.Build();
}

std::vector<std::byte> MeshBox() {
    OnyxBox::Writer w;
    w.AddMesh("mesh", {0.8f, 0.2f, 0.1f, 1.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 3.0f});
    return w.Build();
}

std::vector<std::byte> TableBox() {
    OnyxBox::Writer w;
    w.AddTable("tbl", {{"a", "b"}, {"c", "d"}, {"e", "f"}});
    return w.Build();
}

std::vector<std::byte> PakBox() {
    OnyxBox::Writer a;
    a.AddText("shared.txt", "FROM-BOX-A");
    OnyxBox::Writer b;
    b.AddText("shared.txt", "FROM-BOX-B");
    b.AddRange("corrupt.bin", OnyxBox::Kind::Blob, 0x100000, 4);
    return OnyxBox::Writer::BuildPak({{"boxA.obx", a.Build()}, {"boxB.obx", b.Build()}});
}

// Outranks the real module on a genuine OBX1 file but parses nothing.
struct DecoyModule : Modules::IGameModule {
    Modules::ModuleInfo Info() const override { return {"decoy", "Decoy", {"decoy"}, {}}; }
    Modules::ProbeResult Probe(const Modules::ProbeInput&) const override { return {99, "always outranks obx"}; }
    void RegisterTypes(Types::TypeRegistrar&) override {}
    void RegisterDecoders(Modules::DecoderRegistry&) override {}
    Modules::ParseResult ParseContainer(Modules::ContainerContext&) override { return {true}; }
};

struct NeverMatchModule : Modules::IGameModule {
    Modules::ModuleInfo Info() const override { return {"nevermatch", "NeverMatch", {}, {}}; }
    Modules::ProbeResult Probe(const Modules::ProbeInput&) const override { return {0, "never matches"}; }
    void RegisterTypes(Types::TypeRegistrar&) override {}
    void RegisterDecoders(Modules::DecoderRegistry&) override {}
    Modules::ParseResult ParseContainer(Modules::ContainerContext&) override { return {false}; }
};

struct Harness {
    Modules::Workspace ws;
    Cli::VerbTable table;

    Harness() {
        table.Add(Cli::StockVerbs());
        REQUIRE(ws.AddModule(std::make_unique<OnyxBox::Module>()).ok());
    }

    struct Outcome {
        int rc = 0;
        std::string out;
        std::string err;
    };

    Outcome Invoke(std::vector<std::string> args) {
        args.insert(args.begin(), "onyxbox-cli");
        std::vector<char*> argv;
        for (std::string& a : args) argv.push_back(a.data());
        std::ostringstream out, err;
        const int rc = Cli::Run(ws, table, static_cast<int>(argv.size()), argv.data(), out, err);
        return {rc, out.str(), err.str()};
    }
};

}

TEST_CASE("Cli.ObxProbePrintsTheRowAndTheWinner") {
    Harness h;
    TempFile file("probe.obx", SampleBox());

    const auto r = h.Invoke({"probe", file.path.string()});
    CHECK(r.rc == Cli::kOk);
    CHECK(Contains(r.out, "obx"));
    CHECK(Contains(r.out, "95"));
    CHECK(Contains(r.out, "winner: obx"));
}

TEST_CASE("Cli.ObxListJsonContainsTheTypeKeyAndTheFailedFlag") {
    Harness h;
    TempFile file("list.obx", SampleBox());

    const auto r = h.Invoke({"list", file.path.string(), "--json"});
    CHECK(r.rc == Cli::kOk);
    CHECK(Contains(r.out, "\"type\":\"obx.image\""));
    CHECK(Contains(r.out, "\"failed\":true"));
    CHECK(Contains(r.out, "\"path\":"));
    CHECK(Contains(r.out, "\"module\":\"obx\""));
    CHECK(Contains(r.out, "obx.entry.range"));
}

TEST_CASE("Cli.ObxExtractWritesPayloadBytesAndSkipsFailedEntries") {
    Harness h;
    TempFile file("extract.obx", SampleBox());
    TempPath dir("extract_out");

    const auto r = h.Invoke({"extract", file.path.string(), dir.path.string()});
    CHECK(r.rc == Cli::kOk);
    REQUIRE(std::filesystem::exists(dir.path / "img"));
    REQUIRE(std::filesystem::exists(dir.path / "txt"));
    CHECK_FALSE(std::filesystem::exists(dir.path / "bad"));
    CHECK(std::filesystem::file_size(dir.path / "img") == 260);
    CHECK(ReadFile(dir.path / "txt") == "hello box");
}

TEST_CASE("Cli.ObxExtractSkipsUnsafeEntryNamesAndWritesNothingOutsideTheDir") {
    Harness h;
    TempFile file("unsafe.obx", UnsafeNamesBox());
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
    CHECK_FALSE(std::filesystem::exists(dir.path / "con.txt"));
    size_t count = 0;
    for (const auto& item : std::filesystem::directory_iterator(dir.path)) {
        (void)item;
        ++count;
    }
    CHECK(count == 1);
}

TEST_CASE("Cli.ObxDecodeTextEntryPrintsHelloBox") {
    Harness h;
    TempFile file("decode_text.obx", SampleBox());

    const auto r = h.Invoke({"decode", file.path.string(), "txt"});
    CHECK(r.rc == Cli::kOk);
    CHECK(Contains(r.out, "hello box"));
}

TEST_CASE("Cli.ObxDecodeStrictReturnsKStrictErrorsWhenTheDocumentCarriesAnErrorDiag") {
    Harness h;
    TempFile file("decode_strict.obx", SampleBox());

    CHECK(h.Invoke({"decode", file.path.string(), "txt", "--strict"}).rc == Cli::kStrictErrors);
}

TEST_CASE("Cli.ObxDecodeImageSummarisesTheDimensions") {
    Harness h;
    TempFile file("decode_image.obx", SampleBox());

    const auto r = h.Invoke({"decode", file.path.string(), "img"});
    CHECK(r.rc == Cli::kOk);
    CHECK(Contains(r.out, "image img 8x8 rgba8"));
}

TEST_CASE("Cli.ObxDecodeSceneEntryPrintsThePartsMaterialsAndVertices") {
    Harness h;
    TempFile file("decode_mesh.obx", MeshBox());

    const auto r = h.Invoke({"decode", file.path.string(), "mesh"});
    CHECK(r.rc == Cli::kOk);
    CHECK(Contains(r.out, "scene mesh parts=1 materials=1 vertices=24 triangles=12"));
}

TEST_CASE("Cli.ObxDecodeTableEntryPrintsTheColumnAndRowCounts") {
    Harness h;
    TempFile file("decode_table.obx", TableBox());

    const auto r = h.Invoke({"decode", file.path.string(), "tbl"});
    CHECK(r.rc == Cli::kOk);
    CHECK(Contains(r.out, "table tbl columns=2 rows=3"));
}

TEST_CASE("Cli.ObxDecodeToHandsTheDecodedSceneToTheRegisteredExporter") {
    Harness h;
    TempFile file("export.obx", MeshBox());
    TempPath out("export_out.txt");
    h.table.Exporters().Add("txt", Domain::MediaKind::Mesh, [](const void* payload, const std::filesystem::path& to) {
        const auto* scene = static_cast<const Domain::Scene*>(payload);
        std::ofstream f(to);
        f << "parts=" << scene->parts.size() << " vertices=" << scene->parts.at(0).positions.size();
        return Result<void>::Ok();
    });

    const auto r = h.Invoke({"decode", file.path.string(), "mesh", "--to", "txt", "--out", out.path.string()});
    CHECK(r.rc == Cli::kOk);
    CHECK(Contains(r.out, "exported mesh to"));
    CHECK(ReadFile(out.path) == "parts=1 vertices=24");

    TempFile text("export_text.obx", SampleBox());
    CHECK(h.Invoke({"decode", text.path.string(), "txt", "--to", "txt", "--out", out.path.string()}).rc ==
          Cli::kUsage);
}

TEST_CASE("Cli.ObxDecodeUnknownEntryNameReturnsKUsage") {
    Harness h;
    TempFile file("decode_unknown.obx", SampleBox());

    CHECK(h.Invoke({"decode", file.path.string(), "does-not-exist"}).rc == Cli::kUsage);
}

TEST_CASE("Cli.ObxDecodeReturnsKOkWhenACapabilityExistsButTheDecoderSalvageFails") {
    Harness h;
    TempFile file("decode_liar.obx", LyingImageBox());

    const auto r = h.Invoke({"decode", file.path.string(), "liar"});
    CHECK(r.rc == Cli::kOk);
    CHECK(Contains(r.out, "obx.image.size-mismatch"));
    CHECK_FALSE(Contains(r.out, "image liar"));
}

TEST_CASE("Cli.ObxNoModuleAcceptsAGarbageFile") {
    Harness h;
    REQUIRE(h.ws.AddModule(std::make_unique<NeverMatchModule>()).ok());
    TempFile garbage("garbage.bin", {std::byte{'j'}, std::byte{'u'}, std::byte{'n'}, std::byte{'k'}});

    const auto probe = h.Invoke({"probe", garbage.path.string()});
    CHECK(probe.rc == Cli::kNoModule);
    CHECK(Contains(probe.out, "winner: none"));
    CHECK(h.Invoke({"list", garbage.path.string()}).rc == Cli::kNoModule);
}

TEST_CASE("Cli.ObxRunDispatchesProbeListAndDecodeByName") {
    Harness h;
    TempFile file("dispatch.obx", SampleBox());

    CHECK(Contains(h.Invoke({"probe", file.path.string()}).out, "winner"));
    CHECK(Contains(h.Invoke({"decode", file.path.string(), "txt"}).out, "hello box"));
    CHECK(Contains(h.Invoke({"list", file.path.string(), "--json"}).out, "\"type\":\"obx.image\""));
}

TEST_CASE("Cli.ObxGameHintOverridesRankingToPickTheRealModule") {
    Harness h;
    REQUIRE(h.ws.AddModule(std::make_unique<DecoyModule>()).ok());
    TempFile file("hint.obx", SampleBox());

    const auto ranked = h.Invoke({"list", file.path.string(), "--json"});
    CHECK(ranked.rc == Cli::kOk);
    CHECK(Contains(ranked.out, "\"module\":\"decoy\""));
    CHECK(Contains(ranked.out, "\"entries\":[]"));

    const auto hinted = h.Invoke({"list", file.path.string(), "--json", "--game", "obx"});
    CHECK(hinted.rc == Cli::kOk);
    CHECK(Contains(hinted.out, "\"module\":\"obx\""));
    CHECK(Contains(hinted.out, "\"type\":\"obx.image\""));
}

TEST_CASE("Cli.ParseKeepsA0xFFFFFFFFDeclaredSizeUntruncatedInSourceSizeAndFlagsTheEntryFailed") {
    Harness h;
    TempFile file("huge.obx", HugeSizeBox());

    const Modules::DocumentId id = h.ws.Open(file.path);
    REQUIRE(id != 0);
    Modules::Document* doc = h.ws.Get(id);
    REQUIRE(doc);
    REQUIRE(doc->roots.size() == 1);

    const Domain::AssetEntry& entry = doc->roots[0];
    CHECK(entry.source.size == 0xFFFFFFFFull);
    CHECK(Domain::HasFlag(entry.flags, Domain::NodeFlags::Failed));
}

TEST_CASE("Cli.ObxPakIsProvenEndToEndThroughTheVerbs") {
    Harness h;
    TempFile file("pak.obxpak", PakBox());
    TempPath dir("pak_out");

    const auto probe = h.Invoke({"probe", file.path.string()});
    CHECK(probe.rc == Cli::kOk);
    CHECK(Contains(probe.out, "winner: obx"));
    CHECK(Contains(probe.out, "95"));

    const auto list = h.Invoke({"list", file.path.string(), "--json"});
    CHECK(list.rc == Cli::kOk);
    CHECK(Contains(list.out, "\"name\":\"boxA.obx\""));
    CHECK(Contains(list.out, "\"name\":\"boxB.obx\""));
    size_t shared = 0;
    for (size_t pos = list.out.find("\"name\":\"shared.txt\""); pos != std::string::npos;
         pos = list.out.find("\"name\":\"shared.txt\"", pos + 1)) {
        ++shared;
    }
    CHECK(shared == 2);
    CHECK(Contains(list.out, "\"name\":\"corrupt.bin\""));
    CHECK(Contains(list.out, "\"failed\":true"));

    const auto extract = h.Invoke({"extract", file.path.string(), dir.path.string()});
    CHECK(extract.rc == Cli::kOk);
    REQUIRE(std::filesystem::exists(dir.path / "1" / "shared.txt"));
    REQUIRE(std::filesystem::exists(dir.path / "2" / "shared.txt"));
    CHECK(ReadFile(dir.path / "1" / "shared.txt") == "FROM-BOX-A");
    CHECK(ReadFile(dir.path / "2" / "shared.txt") == "FROM-BOX-B");
    CHECK_FALSE(std::filesystem::exists(dir.path / "2" / "corrupt.bin"));
    CHECK(Contains(extract.out, "obx.entry.range"));

    CHECK(h.Invoke({"decode", file.path.string(), "shared.txt", "--strict"}).rc == Cli::kUsage);
    CHECK(h.Invoke({"decode", file.path.string(), "shared.txt", "--strict", "--first"}).rc == Cli::kStrictErrors);
}

TEST_CASE("decode opens a file whose name is non-ASCII when argv is UTF-8") {
    TempPath root("utf8_argv");
    const std::filesystem::path dir = root.path / std::filesystem::path(u8"caf\u00e9-\u00fc");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    REQUIRE_FALSE(ec);
    const std::filesystem::path file = dir / std::filesystem::path(u8"b\u00f6x.obx");
    {
        const std::vector<std::byte> bytes = SampleBox();
        std::ofstream f(file, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(f.good());
    }

    Harness h;
    const auto r = h.Invoke({"decode", Services::Paths::ToUtf8(file), "txt"});
    CHECK(r.rc == Cli::kOk);
    CHECK(Contains(r.out, "text txt: hello box"));

    const auto listed = h.Invoke({"list", Services::Paths::ToUtf8(file)});
    CHECK(listed.rc == Cli::kOk);
}

TEST_CASE("decode opens a file named outside the ANSI code page and prints its path") {
    TempPath root("utf8_wide");
    const std::filesystem::path dir = root.path / std::filesystem::path(u8"\u65e5\u672c");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    REQUIRE_FALSE(ec);
    const std::filesystem::path file = dir / std::filesystem::path(u8"\u65e5.obx");
    {
        const std::vector<std::byte> bytes = SampleBox();
        std::ofstream f(file, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(f.good());
    }

    Harness h;
    const std::string utf8 = Services::Paths::ToUtf8(file);
    CHECK(h.Invoke({"decode", utf8, "txt"}).rc == Cli::kOk);

    const auto listed = h.Invoke({"list", utf8, "--json"});
    CHECK(listed.rc == Cli::kOk);
    CHECK(Contains(listed.out, Services::Paths::ToUtf8(file.filename())));

    const auto refused = h.Invoke({"list", utf8, "--game", "no-such-module"});
    CHECK(refused.rc == Cli::kNoModule);
    CHECK(Contains(refused.out, "no module accepts " + utf8));

    const std::string exportDir = Services::Paths::ToUtf8(dir / std::filesystem::path(u8"out"));
    const auto extracted = h.Invoke({"extract", utf8, exportDir});
    CHECK(extracted.rc != Cli::kNoModule);
    CHECK(std::filesystem::exists(dir / std::filesystem::path(u8"out") / "txt"));
}
