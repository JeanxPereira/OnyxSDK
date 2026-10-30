#include <doctest/doctest.h>

#include <Onyx/Modules/Workspace.h>
#include <Onyx/Types/TypeCatalog.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
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

// Regression fixture for the Close()-vs-in-flight-OpenAsync-parse UAF:
// ParseContainer signals `started`, then spin-waits on `release` (set by
// the test only after it has confirmed the worker is inside the parse and
// has closed the document), so the test can deterministically land Close()
// while the worker is still holding a reference into the Document.
struct SlowFake : Onyx::Modules::IGameModule {
    std::atomic<bool>* started = nullptr;
    std::atomic<bool>* release = nullptr;
    std::atomic<bool>* finished = nullptr;

    ModuleInfo Info() const override {
        return ModuleInfo{"slowfake", "SlowFake", {}, {}};
    }

    ProbeResult Probe(const ProbeInput& in) const override {
        if (!in.header.empty() && in.header[0] == std::byte{'S'}) {
            return ProbeResult{90, "'S' magic"};
        }
        return ProbeResult{0, "no magic"};
    }

    void RegisterTypes(Onyx::Types::TypeRegistrar&) override {}
    void RegisterDecoders(DecoderRegistry&) override {}

    ParseResult ParseContainer(ContainerContext& ctx) override {
        started->store(true);
        while (!release->load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Onyx::Domain::AssetEntry entry;
        entry.name = "e0";
        ctx.roots.push_back(std::move(entry));
        finished->store(true);
        return ParseResult{true};
    }
};

// Regression fixture for exception containment: a module that throws must
// never let that exception cross Workspace's boundary.
struct ThrowFake : Onyx::Modules::IGameModule {
    ModuleInfo Info() const override {
        return ModuleInfo{"throwfake", "ThrowFake", {}, {}};
    }

    ProbeResult Probe(const ProbeInput& in) const override {
        if (!in.header.empty() && in.header[0] == std::byte{'T'}) {
            return ProbeResult{90, "'T' magic"};
        }
        return ProbeResult{0, "no magic"};
    }

    void RegisterTypes(Onyx::Types::TypeRegistrar&) override {}
    void RegisterDecoders(DecoderRegistry&) override {}

    ParseResult ParseContainer(ContainerContext&) override {
        throw std::runtime_error("boom");
    }
};

// Regression fixture for spec §5.1: with exactly one registered module,
// probing (and its confidence floor) must be skipped entirely. Probe
// always scores well below kProbeFloor here to prove Open still succeeds
// and parses.
struct LowScoreFake : Onyx::Modules::IGameModule {
    ModuleInfo Info() const override {
        return ModuleInfo{"lowscore", "LowScore", {}, {}};
    }

    ProbeResult Probe(const ProbeInput&) const override {
        return ProbeResult{10, "always low"};   // well below kProbeFloor
    }

    void RegisterTypes(Onyx::Types::TypeRegistrar&) override {}
    void RegisterDecoders(DecoderRegistry&) override {}

    ParseResult ParseContainer(ContainerContext& ctx) override {
        Onyx::Domain::AssetEntry entry;
        entry.name = "solo";
        ctx.roots.push_back(std::move(entry));
        return ParseResult{true};
    }
};

// Fixture for CancelOpen: signals `started`, then blocks in
// ParseContainer polling Progress::CancelRequested() (the cooperative
// contract Jobs.h documents) until it observes the flag set, at which
// point it returns without producing anything -- exactly what a module
// honoring a cancel request is expected to do.
struct CancelFake : Onyx::Modules::IGameModule {
    std::atomic<bool>* started = nullptr;

    ModuleInfo Info() const override {
        return ModuleInfo{"cancelfake", "CancelFake", {}, {}};
    }

    ProbeResult Probe(const ProbeInput& in) const override {
        if (!in.header.empty() && in.header[0] == std::byte{'C'}) {
            return ProbeResult{90, "'C' magic"};
        }
        return ProbeResult{0, "no magic"};
    }

    void RegisterTypes(Onyx::Types::TypeRegistrar&) override {}
    void RegisterDecoders(DecoderRegistry&) override {}

    ParseResult ParseContainer(ContainerContext& ctx) override {
        if (started) started->store(true);
        while (!ctx.progress.CancelRequested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return ParseResult{false};
    }
};

// Regression fixture for ContainerContext::path (the container-path gap:
// modules had no way to learn which file they were parsing, so they could
// not fill Domain::AssetEntry::wadName or do path-relative work at parse
// time). Captures what ParseContainer actually received so the test can
// assert it matches the path Workspace was told to open.
struct PathCaptureFake : Onyx::Modules::IGameModule {
    std::filesystem::path seenPath;
    bool sawParseContainer = false;

    ModuleInfo Info() const override {
        return ModuleInfo{"pathcapture", "PathCapture", {}, {}};
    }

    ProbeResult Probe(const ProbeInput& in) const override {
        if (!in.header.empty() && in.header[0] == std::byte{'P'}) {
            return ProbeResult{90, "'P' magic"};
        }
        return ProbeResult{0, "no magic"};
    }

    void RegisterTypes(Onyx::Types::TypeRegistrar&) override {}
    void RegisterDecoders(DecoderRegistry&) override {}

    ParseResult ParseContainer(ContainerContext& ctx) override {
        sawParseContainer = true;
        seenPath = ctx.path;
        return ParseResult{true};
    }
};

} // namespace

TEST_CASE("ContainerContext::path names the file Workspace::Open was told to open") {
    auto tmp = write_temp_file("onyx_workspace_test_path_sync.bin", "Prest");
    {
        Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
        auto mod = std::make_unique<PathCaptureFake>();
        PathCaptureFake* raw = mod.get();
        ws.AddModule(std::move(mod));

        auto id = ws.Open(tmp);
        REQUIRE(id != 0);
        REQUIRE(raw->sawParseContainer);
        CHECK(raw->seenPath == tmp);

        auto* doc = ws.Get(id);
        REQUIRE(doc);
        CHECK(doc->path == tmp);
        CHECK(raw->seenPath == doc->path);   // same value ContainerContext handed the module
    }
    std::filesystem::remove(tmp);
}

TEST_CASE("ContainerContext::path names the file Workspace::OpenAsync was told to open") {
    auto tmp = write_temp_file("onyx_workspace_test_path_async.bin", "Prest");
    {
        Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
        auto mod = std::make_unique<PathCaptureFake>();
        PathCaptureFake* raw = mod.get();
        ws.AddModule(std::move(mod));

        Onyx::Modules::DocumentId readyId = 0;
        auto sub = ws.Events().On<Onyx::Modules::TreeReady>([&](auto& e) { readyId = e.id; });

        auto id = ws.OpenAsync(tmp);
        REQUIRE(id != 0);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (readyId == 0 && std::chrono::steady_clock::now() < deadline) {
            ws.Jobs().Pump();
            ws.Events().Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(readyId == id);
        REQUIRE(raw->sawParseContainer);
        CHECK(raw->seenPath == tmp);
    }
    std::filesystem::remove(tmp);
}

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
    // Two modules registered (spec §5.1 only skips probing -- and its
    // floor -- when exactly one module is registered), and neither
    // recognizes this content: BoxFake wants 'B', ThrowFake wants 'T'.
    ws.AddModule(std::make_unique<BoxFake>());
    ws.AddModule(std::make_unique<ThrowFake>());

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

TEST_CASE("AddModule rejects a duplicate module id") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(std::make_unique<BoxFake>());
    REQUIRE(ws.Modules().size() == 1);

    // Second module with the same Info().id ("boxfake") must be refused,
    // not silently added or allowed to shadow the first registration.
    ws.AddModule(std::make_unique<BoxFake>());
    CHECK(ws.Modules().size() == 1);
}

TEST_CASE("A single registered module skips the probe floor entirely") {
    auto tmp = write_temp_file("onyx_workspace_test_singlemodule.bin", "anything at all");
    {
        Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
        ws.AddModule(std::make_unique<LowScoreFake>());

        // No hint given, and the only module's Probe() would score 10
        // (well below kProbeFloor) -- with more than one module
        // registered this would yield no winner and Open would fail.
        // With exactly one module, spec §5.1 says the probe step is
        // skipped entirely.
        auto id = ws.Open(tmp);
        REQUIRE(id != 0);

        auto* doc = ws.Get(id);
        REQUIRE(doc);
        CHECK(doc->ready);
        REQUIRE(doc->roots.size() == 1);
        CHECK(doc->roots[0].name == "solo");
    } // ~Workspace closes the Document's OsFile before we try to remove it.

    std::filesystem::remove(tmp);
}

TEST_CASE("Close during an in-flight OpenAsync parse keeps the Document alive for the worker") {
    auto tmp = write_temp_file("onyx_workspace_test_uaf.bin", "Srest");
    {
        Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());

        std::atomic<bool> started{false}, release{false}, finished{false};
        auto mod = std::make_unique<SlowFake>();
        mod->started = &started;
        mod->release = &release;
        mod->finished = &finished;
        ws.AddModule(std::move(mod));

        Onyx::Modules::DocumentId sawTreeReadyFor = 0;
        auto sub = ws.Events().On<Onyx::Modules::TreeReady>(
            [&](auto& e) { sawTreeReadyFor = e.id; });

        auto id = ws.OpenAsync(tmp);
        REQUIRE(id != 0);

        // Deterministic hand-off #1: wait for the worker to be inside
        // ParseContainer, holding a reference into this Document.
        auto deadline1 = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!started.load() && std::chrono::steady_clock::now() < deadline1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(started.load());

        // Close while the worker is still mid-parse. Under the old
        // std::vector<std::unique_ptr<Document>> storage this erase would
        // free the Document the worker thread is still touching.
        ws.Close(id);
        CHECK(ws.Get(id) == nullptr);

        // Let the worker finish and wait (deterministically) for it to do so.
        release.store(true);
        auto deadline2 = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!finished.load() && std::chrono::steady_clock::now() < deadline2) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(finished.load());

        // Drain both queues so the Done callback -- if it wrongly ran --
        // would have posted TreeReady by now.
        auto deadline3 = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (std::chrono::steady_clock::now() < deadline3) {
            ws.Jobs().Pump();
            ws.Events().Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        CHECK(ws.Get(id) == nullptr);
        CHECK(sawTreeReadyFor != id);   // no TreeReady for a Closed document
    } // ~Workspace joins the worker (already finished) before removing tmp.

    std::filesystem::remove(tmp);
}

TEST_CASE("A throwing ParseContainer is contained: Open still returns a document with diags") {
    auto tmp = write_temp_file("onyx_workspace_test_throw.bin", "Trest");
    {
        Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
        ws.AddModule(std::make_unique<ThrowFake>());

        Onyx::Modules::DocumentId readyId = 0;
        bool readyOk = true;
        auto sub = ws.Events().On<Onyx::Modules::TreeReady>([&](auto& e) {
            readyId = e.id;
            readyOk = e.ok;
        });

        Onyx::Modules::DocumentId id = 0;
        CHECK_NOTHROW(id = ws.Open(tmp));
        REQUIRE(id != 0);

        ws.Events().Pump();
        CHECK(readyId == id);
        CHECK_FALSE(readyOk);

        auto* doc = ws.Get(id);
        REQUIRE(doc);
        CHECK(doc->diags.HasErrors());
        CHECK(doc->ready);
    }

    std::filesystem::remove(tmp);
}

TEST_CASE("CancelOpen cancels a blocked async parse and unknown ids report false") {
    auto tmp = write_temp_file("onyx_workspace_test_cancel.bin", "Crest");
    {
        Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());

        std::atomic<bool> started{false};
        auto mod = std::make_unique<CancelFake>();
        mod->started = &started;
        ws.AddModule(std::move(mod));

        Onyx::Modules::DocumentId readyId = 0;
        bool readyOk = true;
        auto sub = ws.Events().On<Onyx::Modules::TreeReady>([&](auto& e) {
            readyId = e.id;
            readyOk = e.ok;
        });

        auto id = ws.OpenAsync(tmp);
        REQUIRE(id != 0);

        // Deterministic hand-off: wait for the worker to be inside
        // ParseContainer, polling CancelRequested().
        auto deadline1 = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!started.load() && std::chrono::steady_clock::now() < deadline1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(started.load());

        CHECK(ws.CancelOpen(id) == true);
        CHECK(ws.CancelOpen(Onyx::Modules::DocumentId(999999)) == false);

        auto deadline2 = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (readyId == 0 && std::chrono::steady_clock::now() < deadline2) {
            ws.Jobs().Pump();
            ws.Events().Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        REQUIRE(readyId == id);
        CHECK_FALSE(readyOk);   // module honored the cancel, reported ParseResult{false}

        auto* doc = ws.Get(id);
        REQUIRE(doc);
        CHECK(doc->ready);
    } // ~Workspace joins the worker (already finished) before removing tmp.

    std::filesystem::remove(tmp);
}

TEST_CASE("SetWorkspaceSettingsPath swaps in Settings loaded from that path") {
    auto tmp = std::filesystem::temp_directory_path() / "onyx_workspace_test_settings.toml";
    std::error_code rmEc;
    std::filesystem::remove(tmp, rmEc);

    {
        Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
        ws.SetWorkspaceSettingsPath(tmp);

        ws.WorkspaceSettings().Set("gowr.texIndexDir", std::string("cache/tex"));
        REQUIRE(ws.WorkspaceSettings().Save());
    }

    // Reload via a fresh Settings::Load, independent of the Workspace
    // instance that wrote it -- proves the value actually round-tripped
    // through the path SetWorkspaceSettingsPath pointed at, not just
    // through the in-memory instance.
    Onyx::Services::Settings reloaded = Onyx::Services::Settings::Load(tmp);
    CHECK(reloaded.GetString("gowr.texIndexDir") == "cache/tex");

    std::filesystem::remove(tmp, rmEc);
}
