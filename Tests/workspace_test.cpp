#include <doctest/doctest.h>

#include <Onyx/Modules/Workspace.h>
#include <Onyx/Types/TypeCatalog.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

using namespace Onyx::Modules;

namespace {

std::filesystem::path write_temp_file(const std::string& name, const std::string& contents) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream f(path, std::ios::binary);
    f << contents;
    f.close();
    return path;
}

// Fake that parses a "tree" of N entries from the first byte of the file,
// marks entry index 1 Failed with a diag, and stashes ModuleState.
//
// Info: id "boxfake", hint "bf". Probe: header[0]=='B' -> 90 else 0.
// ParseContainer: reads byte 1 = count; emits count entries named
// "e0".."eN-1" (typeId from RegisterTypes minting "boxfake.item");
// entry 1 (when present) gets flags=Failed plus a diag
// {Error, "boxfake.item.bad", "entry 1 corrupt"}; state = count.
struct BoxFake : Onyx::Modules::IGameModule {
    Onyx::Types::TypeId itemType;

    ModuleInfo Info() const override {
        return ModuleInfo{"boxfake", "BoxFake", {"bf"}, {}};
    }

    ProbeResult Probe(const ProbeInput& in) const override {
        if (!in.header.empty() && in.header[0] == std::byte{'B'}) {
            return ProbeResult{90, "'B' magic"};
        }
        return ProbeResult{0, "no magic"};
    }

    void RegisterTypes(Onyx::Types::TypeRegistrar& r) override {
        Onyx::Types::TypeInfo info;
        info.key = "item";
        info.label = "Box Item";
        itemType = r.Add(info);
    }

    void RegisterDecoders(DecoderRegistry&) override {}

    ParseResult ParseContainer(ContainerContext& ctx) override {
        uint8_t header[2] = {0, 0};
        ctx.file.Seek(0, 0); // SEEK_SET
        size_t n = ctx.file.Read(header, sizeof(header));
        if (n < 2) return ParseResult{false};

        uint8_t count = header[1];
        ctx.state = std::make_shared<uint8_t>(count);

        for (uint8_t i = 0; i < count; ++i) {
            Onyx::Domain::AssetEntry entry;
            entry.name = "e" + std::to_string(i);
            entry.typeId = itemType;
            if (i == 1) {
                entry.flags = Onyx::Domain::NodeFlags::Failed;
                ctx.diags.Report(Onyx::Services::Diag{
                    Onyx::Services::Severity::Error,
                    "boxfake.item.bad",
                    "entry 1 corrupt",
                    std::nullopt});
            }
            ctx.roots.push_back(std::move(entry));
        }
        return ParseResult{true};
    }
};

} // namespace

TEST_CASE("Workspace opens a document through probe and salvages") {
    auto tmp = write_temp_file("onyx_workspace_test_salvage.bin", "B\x03rest");
    {
        Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
        ws.AddModule(std::make_unique<BoxFake>());

        int opened = 0;
        Onyx::Modules::DocumentId readyId = 0;
        auto s1 = ws.Events().On<Onyx::Modules::DocumentOpened>([&](auto&) { ++opened; });
        auto s2 = ws.Events().On<Onyx::Modules::TreeReady>([&](auto& e) { readyId = e.id; });

        auto id = ws.Open(tmp);
        REQUIRE(id != 0);

        ws.Events().Pump();
        CHECK(opened == 1);
        CHECK(readyId == id);

        auto* doc = ws.Get(id);
        REQUIRE(doc);
        REQUIRE(doc->roots.size() == 3);
        CHECK(doc->roots[1].flags == Onyx::Domain::NodeFlags::Failed);
        CHECK(doc->diags.HasErrors());               // salvage: tree + diags
        CHECK(doc->roots[0].flags == Onyx::Domain::NodeFlags::None);
        CHECK(doc->ready);
    } // ~Workspace closes every Document's OsFile, releasing the OS handle
      // (Windows refuses to remove an open file).

    std::filesystem::remove(tmp);
}

TEST_CASE("No module accepts the file: Open returns 0") {
    auto tmp = write_temp_file("onyx_workspace_test_nomatch.bin", "Xrest");
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(std::make_unique<BoxFake>());

    int opened = 0;
    auto sub = ws.Events().On<Onyx::Modules::DocumentOpened>([&](auto&) { ++opened; });

    auto id = ws.Open(tmp);
    CHECK(id == 0);

    ws.Events().Pump();
    CHECK(opened == 0);   // no document was ever created

    std::filesystem::remove(tmp);
}

TEST_CASE("OpenAsync delivers TreeReady through the pumps") {
    auto tmp = write_temp_file("onyx_workspace_test_async.bin", "B\x02rest");
    {
        Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
        ws.AddModule(std::make_unique<BoxFake>());

        Onyx::Modules::DocumentId readyId = 0;
        bool readyOk = false;
        auto sub = ws.Events().On<Onyx::Modules::TreeReady>([&](auto& e) {
            readyId = e.id;
            readyOk = e.ok;
        });

        auto id = ws.OpenAsync(tmp);
        REQUIRE(id != 0);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (readyId == 0 && std::chrono::steady_clock::now() < deadline) {
            ws.Jobs().Pump();
            ws.Events().Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        REQUIRE(readyId == id);
        CHECK(readyOk);

        auto* doc = ws.Get(id);
        REQUIRE(doc);
        CHECK(doc->ready);
        CHECK(doc->roots.size() == 2);
    } // ~Workspace closes every Document's OsFile before we try to remove it.

    std::filesystem::remove(tmp);
}

TEST_CASE("Close posts DocumentClosed and Get returns null after") {
    auto tmp = write_temp_file("onyx_workspace_test_close.bin", "B\x01rest");
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(std::make_unique<BoxFake>());

    auto id = ws.Open(tmp);
    REQUIRE(id != 0);
    REQUIRE(ws.Get(id) != nullptr);

    Onyx::Modules::DocumentId closedId = 0;
    auto sub = ws.Events().On<Onyx::Modules::DocumentClosed>([&](auto& e) { closedId = e.id; });

    ws.Close(id);
    ws.Events().Pump();

    CHECK(closedId == id);
    CHECK(ws.Get(id) == nullptr);

    std::filesystem::remove(tmp);
}

TEST_CASE("FindModule resolves by id then hint") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    auto mod = std::make_unique<BoxFake>();
    BoxFake* raw = mod.get();
    ws.AddModule(std::move(mod));

    CHECK(ws.FindModule("boxfake") == raw);
    CHECK(ws.FindModule("bf") == raw);
    CHECK(ws.FindModule("nope") == nullptr);
}
