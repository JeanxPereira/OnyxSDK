#include <doctest/doctest.h>

#include <OnyxBox/Module.h>
#include <OnyxBox/Writer.h>

#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/Core/Schema/Schema.h>
#include <Onyx/TestKit/DecodeSmoke.h>
#include <Onyx/TestKit/Goldens.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace Onyx;

namespace {

struct TempFile {
    std::filesystem::path path;

    TempFile(const std::string& name, const std::vector<std::byte>& bytes)
        : path(std::filesystem::temp_directory_path() / ("onyx_obx_testkit_" + name)) {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::optional<std::string> GetEnv(const char* name) {
#ifdef _WIN32
    char* buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, name) != 0 || !buf) return std::nullopt;
    std::string value(buf);
    std::free(buf);
    return value;
#else
    const char* v = std::getenv(name);
    if (!v) return std::nullopt;
    return std::string(v);
#endif
}

void SetEnv(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    if (value.empty()) {
        unsetenv(name);
    } else {
        setenv(name, value.c_str(), 1);
    }
#endif
}

struct EnvGuard {
    const char* name;
    std::optional<std::string> saved;

    EnvGuard(const char* n, const std::string& value) : name(n), saved(GetEnv(n)) { SetEnv(name, value); }
    ~EnvGuard() { SetEnv(name, saved.value_or("")); }
};

std::vector<std::byte> Gradient(size_t count) {
    std::vector<std::byte> out(count);
    for (size_t i = 0; i < count; ++i) out[i] = static_cast<std::byte>(i * 7 & 0xFF);
    return out;
}

std::vector<std::byte> TwoEntryBox() {
    OnyxBox::Writer w;
    w.AddImage("img", 8, 8, Gradient(256));
    w.AddText("txt", "hello box");
    return w.Build();
}

std::vector<std::byte> SampleBox() {
    OnyxBox::Writer w;
    w.AddImage("img", 8, 8, Gradient(256));
    w.AddText("txt", "hello box");
    w.AddRange("bad", OnyxBox::Kind::Blob, 0x100000, 4);
    return w.Build();
}

std::vector<std::byte> AllKindsBox() {
    OnyxBox::Writer w;
    w.AddBlob("blob", std::vector<std::byte>(5, std::byte{0x42}));
    w.AddImage("img", 2, 2, Gradient(16));
    w.AddText("txt", "caf\xC3\xA9 \"quoted\"\n");
    w.AddMesh("mesh", {0.5f, 0.5f, 0.5f, 1.0f}, {0.0f, 1.0f, 2.0f}, {1.0f, 1.0f, 1.0f});
    w.AddTable("tbl", {{"a", "b"}, {"c", "d"}});
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

std::unique_ptr<Modules::Workspace> NewWorkspace() {
    auto ws = std::make_unique<Modules::Workspace>();
    REQUIRE(ws->AddModule(std::make_unique<OnyxBox::Module>()).ok());
    return ws;
}

std::string Snapshot(const std::string& name, const std::vector<std::byte>& bytes) {
    TempFile file(name, bytes);
    auto ws = NewWorkspace();
    const Modules::DocumentId id = ws->Open(file.path);
    REQUIRE(id != 0);
    Modules::Document* doc = ws->Get(id);
    REQUIRE(doc);
    return TestKit::SnapshotTree(*doc, ws->Catalog());
}

void CheckAgainstGolden(const std::string& snapshot, const char* golden) {
    const std::filesystem::path path = std::filesystem::path(ONYX_GOLDEN_DIR) / "obx" / golden;
    TestKit::UpdateGolden(snapshot, path);
    std::string diff;
    CHECK_MESSAGE(TestKit::CompareTreeGolden(snapshot, path, diff), diff);
}

// Registers a table type, an any-capability type and a schema type, each over a one-byte entry.
class SlotsModule : public Modules::IGameModule {
public:
    bool failTable = false;

    Modules::ModuleInfo Info() const override { return {"slots", "Slots", {}, {}}; }
    Modules::ProbeResult Probe(const Modules::ProbeInput&) const override { return {90, "slots"}; }

    void RegisterTypes(Types::TypeRegistrar& registrar) override {
        m_table = Add(registrar, "table", Domain::MediaKind::Table);
        m_any = Add(registrar, "any", Domain::MediaKind::Raw);
        m_schema = Add(registrar, "schema", Domain::MediaKind::Raw);
        m_both = Add(registrar, "both", Domain::MediaKind::Raw);
    }

    void RegisterDecoders(Modules::DecoderRegistry& decoders) override {
        const bool failTableNow = failTable;
        const auto table = [failTableNow](Modules::DecodeContext&) -> std::optional<Domain::Table> {
            if (failTableNow) return std::nullopt;
            Domain::Table t;
            t.columns = {"c"};
            t.rows = {{"1"}};
            return t;
        };
        decoders.Table(m_table, table);
        decoders.Table(m_both, table);
        decoders.Any(m_any, "thing", [](Modules::DecodeContext&) { return std::make_shared<int>(7); });
        decoders.Any(m_both, "thing", [](Modules::DecodeContext&) { return std::shared_ptr<void>(); });
        auto schema = std::make_shared<Schema::Schema>("One");
        schema->Field("v", Schema::FieldType::U8);
        decoders.Schema(m_schema, schema);
    }

    Modules::ParseResult ParseContainer(Modules::ContainerContext& ctx) override {
        const std::pair<const char*, Types::TypeId> entries[] = {
            {"table", m_table}, {"any", m_any}, {"schema", m_schema}, {"both", m_both}};
        for (const auto& [name, type] : entries) {
            Domain::AssetEntry e;
            e.name = name;
            e.type = type;
            e.source = {0, 0, 1};
            ctx.roots.push_back(std::move(e));
        }
        return {true};
    }

private:
    static Types::TypeId Add(Types::TypeRegistrar& registrar, const char* key, Domain::MediaKind media) {
        Types::TypeSpec spec;
        spec.key = key;
        spec.label = key;
        spec.media = media;
        return registrar.Add(spec);
    }

    Types::TypeId m_table, m_any, m_schema, m_both;
};

}

TEST_CASE("TestKit.SnapshotTreeOfATwoEntryBoxIsCanonical") {
    TempFile file("two_entry.obx", TwoEntryBox());
    auto ws = NewWorkspace();
    const Modules::DocumentId id = ws->Open(file.path);
    REQUIRE(id != 0);
    Modules::Document* doc = ws->Get(id);
    REQUIRE(doc);
    REQUIRE(doc->roots.size() == 2);

    const std::string a = TestKit::SnapshotTree(*doc, ws->Catalog());
    const std::string b = TestKit::SnapshotTree(*doc, ws->Catalog());
    CHECK(a == b);

    const size_t img = a.find("\"img\"");
    const size_t txt = a.find("\"txt\"");
    REQUIRE(img != std::string::npos);
    REQUIRE(txt != std::string::npos);
    CHECK(img < txt);

    CHECK(a.find("\"key\": \"obx.image\"") != std::string::npos);
    CHECK(a.find("\"key\": \"obx.text\"") != std::string::npos);
    CHECK(a.find("\"kind\": \"Image\"") != std::string::npos);
    CHECK(a.find("\"kind\": \"Text\"") != std::string::npos);
    CHECK(a.find("\"size\": 260") != std::string::npos);
    CHECK(a.find("\"size\": 9") != std::string::npos);
    CHECK(a.find("\"flags\": 0") != std::string::npos);
    CHECK(a.find("\"children\": []") != std::string::npos);
    CHECK(a.back() == '\n');
    CHECK(a.find('\r') == std::string::npos);
}

TEST_CASE("TestKit.SnapshotTreeRecordsFailedFlagsAndNestsChildren") {
    const std::string pak = Snapshot("nested.obxpak", PakBox());

    CHECK(pak.find("\"name\": \"boxB.obx\"") != std::string::npos);
    CHECK(pak.find("\"kind\": \"Container\"") != std::string::npos);
    CHECK(pak.find("\"flags\": 1") != std::string::npos);
    CHECK(pak.find("\"name\": \"corrupt.bin\"") != std::string::npos);
}

TEST_CASE("TestKit.SnapshotTreeEscapesNamesAndHandlesAnEmptyDocument") {
    OnyxBox::Writer w;
    w.AddBlob("quo\"te\\back", {});
    const std::string escaped = Snapshot("escape.obx", w.Build());
    CHECK(escaped.find("\"name\": \"quo\\\"te\\\\back\"") != std::string::npos);

    const std::string empty = Snapshot("empty.obx", OnyxBox::Writer().Build());
    CHECK(empty == "{\n  \"roots\": []\n}\n");
}

TEST_CASE("TestKit.CompareTreeGoldenReportsAPreciseDiffAndWritesActualOnMismatch") {
    const std::string snapshot = Snapshot("compare.obx", TwoEntryBox());
    const std::filesystem::path golden = std::filesystem::temp_directory_path() / "onyx_testkit_golden.json";
    std::filesystem::path actual = golden;
    actual += ".actual";
    std::error_code ec;
    std::filesystem::remove(golden, ec);
    std::filesystem::remove(actual, ec);

    {
        std::string diff;
        CHECK_FALSE(TestKit::CompareTreeGolden(snapshot, golden, diff));
        CHECK_FALSE(diff.empty());
        CHECK(std::filesystem::exists(actual));
    }

    {
        std::ofstream out(golden, std::ios::binary);
        out << snapshot;
    }
    std::filesystem::remove(actual, ec);
    {
        std::string diff;
        CHECK(TestKit::CompareTreeGolden(snapshot, golden, diff));
        CHECK(diff.empty());
        CHECK_FALSE(std::filesystem::exists(actual));
    }

    {
        std::string mutated = snapshot;
        const size_t pos = mutated.find("\"img\"");
        REQUIRE(pos != std::string::npos);
        mutated.replace(pos, 5, "\"imx\"");

        std::string diff;
        CHECK_FALSE(TestKit::CompareTreeGolden(mutated, golden, diff));
        CHECK(diff.find("differs") != std::string::npos);
        CHECK(ReadFile(actual) == mutated);
    }

    {
        std::string shorter = snapshot.substr(0, snapshot.rfind("  ]"));
        std::string diff;
        CHECK_FALSE(TestKit::CompareTreeGolden(shorter, golden, diff));
        CHECK_FALSE(diff.empty());
    }

    std::filesystem::remove(golden, ec);
    std::filesystem::remove(actual, ec);
}

TEST_CASE("TestKit.UpdateGoldenWritesOnlyWhenTheEnvironmentAsksForIt") {
    const std::filesystem::path golden =
        std::filesystem::temp_directory_path() / "onyx_testkit_update" / "nested" / "g.golden.json";
    std::error_code ec;
    std::filesystem::remove_all(golden.parent_path().parent_path(), ec);

    {
        EnvGuard off("ONYX_UPDATE_GOLDENS", "");
        CHECK_FALSE(TestKit::UpdateGolden("first\n", golden));
        CHECK_FALSE(std::filesystem::exists(golden));
    }
    {
        EnvGuard other("ONYX_UPDATE_GOLDENS", "0");
        CHECK_FALSE(TestKit::UpdateGolden("first\n", golden));
        CHECK_FALSE(std::filesystem::exists(golden));
    }
    {
        EnvGuard on("ONYX_UPDATE_GOLDENS", "1");
        CHECK(TestKit::UpdateGolden("first\n", golden));
        CHECK(ReadFile(golden) == "first\n");
        CHECK(TestKit::UpdateGolden("second\n", golden));
        CHECK(ReadFile(golden) == "second\n");
    }

    std::filesystem::remove_all(golden.parent_path().parent_path(), ec);
}

TEST_CASE("TestKit.ObxTreesMatchTheirGoldens") {
    CheckAgainstGolden(Snapshot("golden_sample.obx", SampleBox()), "sample.golden.json");
    CheckAgainstGolden(Snapshot("golden_kinds.obx", AllKindsBox()), "kinds.golden.json");
    CheckAgainstGolden(Snapshot("golden_pak.obxpak", PakBox()), "pak.golden.json");
}

TEST_CASE("TestKit.DecodeAllOnTheTwoEntryBoxDecodesBoth") {
    TempFile file("decode_two.obx", TwoEntryBox());
    auto ws = NewWorkspace();
    const Modules::DocumentId id = ws->Open(file.path);
    REQUIRE(id != 0);

    const TestKit::SmokeResult result = TestKit::DecodeAll(*ws, id);
    CHECK(result.decoded == 2);
    CHECK(result.failed == 0);
    CHECK(result.errors.empty());
}

TEST_CASE("TestKit.DecodeAllOnACorruptEntryFailsAndNamesIt") {
    OnyxBox::Writer w;
    std::vector<std::byte> liar = {std::byte{8}, std::byte{0}, std::byte{8}, std::byte{0}};
    liar.insert(liar.end(), 6, std::byte{0xAA});
    w.AddEntry("liar", OnyxBox::Kind::Image, liar);
    TempFile file("decode_liar.obx", w.Build());
    auto ws = NewWorkspace();
    const Modules::DocumentId id = ws->Open(file.path);
    REQUIRE(id != 0);

    const TestKit::SmokeResult result = TestKit::DecodeAll(*ws, id);
    CHECK(result.failed >= 1);
    CHECK(result.decoded == 0);
    REQUIRE_FALSE(result.errors.empty());
    CHECK(result.errors[0].find("liar") != std::string::npos);

    const TestKit::SmokeResult allowed = TestKit::DecodeAll(*ws, id, {"liar"});
    CHECK(allowed.failed == 0);
    CHECK(allowed.skipped >= 1);
    CHECK(allowed.errors.empty());
}

TEST_CASE("TestKit.DecodeAllSkipsAParseTimeFailedEntryAsSkippedFailed") {
    OnyxBox::Writer w;
    w.AddRange("huge", OnyxBox::Kind::Blob, 0x100000, 0xFFFFFFFFu);
    w.AddText("txt", "hello sibling");
    TempFile file("decode_huge.obx", w.Build());
    auto ws = NewWorkspace();
    const Modules::DocumentId id = ws->Open(file.path);
    REQUIRE(id != 0);
    Modules::Document* doc = ws->Get(id);
    REQUIRE(doc);
    REQUIRE(doc->roots.size() == 2);
    REQUIRE(Domain::HasFlag(doc->roots[0].flags, Domain::NodeFlags::Failed));

    const TestKit::SmokeResult result = TestKit::DecodeAll(*ws, id);
    CHECK(result.skippedFailed == 1);
    CHECK(result.failed == 0);
    CHECK(result.decoded == 1);
    CHECK(result.errors.empty());
}

TEST_CASE("TestKit.DecodeAllCountsAnEntryWithoutDecodersAsSkipped") {
    OnyxBox::Writer w;
    w.AddBlob("blob", std::vector<std::byte>(3, std::byte{1}));
    TempFile file("decode_blob.obx", w.Build());
    auto ws = NewWorkspace();
    const Modules::DocumentId id = ws->Open(file.path);
    REQUIRE(id != 0);

    const TestKit::SmokeResult result = TestKit::DecodeAll(*ws, id);
    CHECK(result.skipped == 1);
    CHECK(result.decoded == 0);
    CHECK(result.failed == 0);
}

TEST_CASE("TestKit.DecodeAllReturnsAnEmptyResultForAnUnknownDocument") {
    auto ws = NewWorkspace();
    const TestKit::SmokeResult result = TestKit::DecodeAll(*ws, 99);
    CHECK(result.decoded == 0);
    CHECK(result.skipped == 0);
    CHECK(result.skippedFailed == 0);
    CHECK(result.failed == 0);
}

TEST_CASE("TestKit.DecodeAllCoversAllTheSlotsOfTheObxTypes") {
    TempFile file("decode_kinds.obx", AllKindsBox());
    auto ws = NewWorkspace();
    const Modules::DocumentId id = ws->Open(file.path);
    REQUIRE(id != 0);

    const TestKit::SmokeResult result = TestKit::DecodeAll(*ws, id);
    CHECK(result.decoded == 4);
    CHECK(result.skipped == 1);
    CHECK(result.failed == 0);
}

TEST_CASE("TestKit.DecodeAllCoversAnyAndTableSlots") {
    TempFile file("decode_slots.bin", {std::byte{0}});

    {
        Modules::Workspace ws;
        auto module = std::make_unique<SlotsModule>();
        REQUIRE(ws.AddModule(std::move(module)).ok());
        const Modules::DocumentId id = ws.Open(file.path);
        REQUIRE(id != 0);

        const TestKit::SmokeResult result = TestKit::DecodeAll(ws, id);
        CHECK(result.decoded == 3);
        CHECK(result.failed == 1);
        REQUIRE(result.errors.size() == 1);
        CHECK(result.errors[0].find("both") != std::string::npos);
        CHECK(result.errors[0].find("thing") != std::string::npos);

        const TestKit::SmokeResult allowed = TestKit::DecodeAll(ws, id, {"both"});
        CHECK(allowed.decoded == 3);
        CHECK(allowed.failed == 0);
        CHECK(allowed.skipped == 1);
    }
    {
        Modules::Workspace ws;
        auto module = std::make_unique<SlotsModule>();
        module->failTable = true;
        REQUIRE(ws.AddModule(std::move(module)).ok());
        const Modules::DocumentId id = ws.Open(file.path);
        REQUIRE(id != 0);

        const TestKit::SmokeResult result = TestKit::DecodeAll(ws, id);
        CHECK(result.decoded == 2);
        CHECK(result.failed == 2);
        bool namesTable = false;
        for (const std::string& e : result.errors) {
            if (e.find("table") != std::string::npos) namesTable = true;
        }
        CHECK(namesTable);
    }
}
