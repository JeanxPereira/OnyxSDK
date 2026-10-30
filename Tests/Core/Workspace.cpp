#include <doctest/doctest.h>

#include <Onyx/Core/Modules/Workspace.h>

#include "Support/StubModule.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <thread>

using namespace Onyx;
using namespace Onyx::Modules;
using Onyx::Tests::StubModule;
using Onyx::Tests::StubOptions;

namespace {

std::filesystem::path WriteTemp(const std::filesystem::path& name, const std::string& contents) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream f(path, std::ios::binary);
    f << contents;
    return path;
}

struct TempFile {
    std::filesystem::path path;
    TempFile(const std::filesystem::path& name, const std::string& contents) : path(WriteTemp(name, contents)) {}
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

template <class Pred>
bool PumpUntil(Workspace& ws, Pred done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!done() && std::chrono::steady_clock::now() < deadline) {
        ws.Pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return done();
}

struct GatedModule : IGameModule {
    std::atomic<bool>* started = nullptr;
    std::atomic<bool>* release = nullptr;
    std::atomic<bool>* finished = nullptr;

    ModuleInfo Info() const override { return {"gated", "Gated", {}, {}}; }
    ProbeResult Probe(const ProbeInput&) const override { return {90, "gated"}; }
    void RegisterTypes(Types::TypeRegistrar&) override {}
    void RegisterDecoders(DecoderRegistry&) override {}
    ParseResult ParseContainer(ContainerContext& ctx) override {
        started->store(true);
        while (!release->load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ctx.roots.push_back({});
        finished->store(true);
        return {true};
    }
};

struct CancelModule : IGameModule {
    std::atomic<bool>* started = nullptr;

    ModuleInfo Info() const override { return {"cancel", "Cancel", {}, {}}; }
    ProbeResult Probe(const ProbeInput&) const override { return {90, "cancel"}; }
    void RegisterTypes(Types::TypeRegistrar&) override {}
    void RegisterDecoders(DecoderRegistry&) override {}
    ParseResult ParseContainer(ContainerContext& ctx) override {
        started->store(true);
        while (!ctx.progress.CancelRequested()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return {false};
    }
};

struct CaptureModule : IGameModule {
    std::filesystem::path seenPath;
    uint64_t seenSize = 0;
    size_t seenFiles = 0;
    bool sawParse = false;

    ModuleInfo Info() const override { return {"capture", "Capture", {}, {}}; }
    ProbeResult Probe(const ProbeInput&) const override { return {90, "capture"}; }
    void RegisterTypes(Types::TypeRegistrar&) override {}
    void RegisterDecoders(DecoderRegistry&) override {}
    ParseResult ParseContainer(ContainerContext& ctx) override {
        sawParse = true;
        seenPath = ctx.path;
        seenSize = ctx.reader.Size();
        seenFiles = ctx.files.size();
        return {true};
    }
};

struct MountModule : IGameModule {
    bool refuse = false;
    bool sawMounted = false;

    ModuleInfo Info() const override { return {"mounty", "Mounty", {}, {}}; }
    ProbeResult Probe(const ProbeInput&) const override { return {90, "mounty"}; }
    void RegisterTypes(Types::TypeRegistrar&) override {}
    void RegisterDecoders(DecoderRegistry&) override {}
    std::vector<MountSpec> Mounts() const override {
        return {{"bin", {"bin"}, [this](const std::filesystem::path&) -> std::shared_ptr<Vfs::IVirtualFileSystem> {
                     if (refuse) return nullptr;
                     struct Empty : Vfs::IVirtualFileSystem {
                         std::vector<Vfs::DirEntry> List(std::string_view) const override { return {}; }
                         std::shared_ptr<Vfs::IFile> Open(std::string_view) const override { return nullptr; }
                         bool Exists(std::string_view) const override { return false; }
                     };
                     return std::make_shared<Empty>();
                 }}};
    }
    ParseResult ParseContainer(ContainerContext& ctx) override {
        sawMounted = ctx.mounted != nullptr;
        return {true};
    }
};

}

TEST_CASE("Workspace.OpensThroughProbeAndReportsEvents") {
    TempFile tmp("onyx_ws_probe.bin", "0123456789abcdef");
    Workspace ws;
    REQUIRE(ws.AddModule(std::make_unique<StubModule>(StubOptions{"stub", 90, 3})));

    int opened = 0;
    DocumentId ready = 0;
    auto s1 = ws.Events().On<DocumentOpened>([&](const DocumentOpened&) { ++opened; });
    auto s2 = ws.Events().On<TreeReady>([&](const TreeReady& e) { ready = e.id; });

    const DocumentId id = ws.Open(tmp.path);
    REQUIRE(id != 0);
    ws.Pump();
    CHECK(opened == 1);
    CHECK(ready == id);

    Document* doc = ws.Get(id);
    REQUIRE(doc);
    CHECK(doc->ready);
    REQUIRE(doc->roots.size() == 3);
    CHECK(doc->roots[2].name == "e2");
    CHECK(doc->roots[1].source.offset == 4);
    REQUIRE(doc->files.size() == 1);
    CHECK(doc->files[0]->Size() == 16);
    CHECK(ws.Documents().size() == 1);
}

TEST_CASE("Workspace.NoModuleAcceptsTheFile") {
    TempFile tmp("onyx_ws_nomatch.bin", "Xrest");
    Workspace ws;
    REQUIRE(ws.AddModule(std::make_unique<StubModule>(StubOptions{"a", 10, 1})));
    REQUIRE(ws.AddModule(std::make_unique<StubModule>(StubOptions{"b", 20, 1})));

    int opened = 0;
    auto sub = ws.Events().On<DocumentOpened>([&](const DocumentOpened&) { ++opened; });
    CHECK(ws.Open(tmp.path) == 0);
    ws.Pump();
    CHECK(opened == 0);
}

TEST_CASE("Workspace.SingleModuleBelowFloorStillRefuses") {
    TempFile tmp("onyx_ws_single.bin", "anything at all");
    Workspace ws;
    REQUIRE(ws.AddModule(std::make_unique<StubModule>(StubOptions{"lonely", kProbeFloor - 1, 1})));
    CHECK(ws.Open(tmp.path) == 0);
    CHECK(ws.Documents().empty());
}

TEST_CASE("Workspace.HintSelectsTheModule") {
    TempFile tmp("onyx_ws_hint.bin", "data");
    Workspace ws;
    REQUIRE(ws.AddModule(std::make_unique<StubModule>(StubOptions{"weak", 5, 1, false, {"w"}})));
    REQUIRE(ws.AddModule(std::make_unique<StubModule>(StubOptions{"strong", 95, 2})));

    const DocumentId viaHint = ws.Open(tmp.path, "w");
    REQUIRE(viaHint != 0);
    CHECK(ws.Get(viaHint)->module == ws.FindModule("weak"));
    CHECK(ws.Get(viaHint)->roots.size() == 1);

    const DocumentId probed = ws.Open(tmp.path);
    REQUIRE(probed != 0);
    CHECK(ws.Get(probed)->module == ws.FindModule("strong"));

    CHECK(ws.Open(tmp.path, "missing") == 0);
}

TEST_CASE("Workspace.FindModuleResolvesByIdThenHint") {
    Workspace ws;
    REQUIRE(ws.AddModule(std::make_unique<StubModule>(StubOptions{"stub", 90, 0, false, {"st"}})));
    IGameModule* module = ws.Modules().front();
    CHECK(ws.FindModule("stub") == module);
    CHECK(ws.FindModule("st") == module);
    CHECK(ws.FindModule("nope") == nullptr);
}

TEST_CASE("Workspace.AddModuleRejectsDuplicateId") {
    Workspace ws;
    REQUIRE(ws.AddModule(std::make_unique<StubModule>()));
    const auto again = ws.AddModule(std::make_unique<StubModule>());
    REQUIRE_FALSE(again.ok());
    CHECK(again.error().code == "workspace.duplicateModule");
    CHECK(ws.Modules().size() == 1);
}

TEST_CASE("Workspace.AddModuleAppliesSettingsDefaults") {
    Workspace ws;
    REQUIRE(ws.AddModule(std::make_unique<StubModule>()));
    CHECK(ws.Settings().GetBool("stub.flag") == true);
    REQUIRE(ws.ModuleSettings("stub").Entries().size() == 1);
    CHECK(ws.ModuleSettings("stub").Entries()[0].key == "stub.flag");
}

TEST_CASE("Workspace.RemoveModuleClosesItsDocumentsAndDecoders") {
    TempFile tmp("onyx_ws_remove.bin", "data");
    Workspace ws;
    auto stub = std::make_unique<StubModule>(StubOptions{"stub", 90, 1});
    StubModule* raw = stub.get();
    REQUIRE(ws.AddModule(std::move(stub)));
    REQUIRE(ws.AddModule(std::make_unique<StubModule>(StubOptions{"other", 20, 1})));

    const Types::TypeId blob = raw->BlobType();
    REQUIRE(blob.Valid());
    CHECK(ws.Decoders().Of(blob).text);

    const DocumentId id = ws.Open(tmp.path, "stub");
    REQUIRE(id != 0);
    DocumentId closed = 0;
    auto sub = ws.Events().On<DocumentClosed>([&](const DocumentClosed& e) { closed = e.id; });

    CHECK(ws.RemoveModule("stub"));
    ws.Pump();
    CHECK(closed == id);
    CHECK(ws.Get(id) == nullptr);
    CHECK(ws.Decoders().Of(blob).Empty());
    CHECK(ws.FindModule("stub") == nullptr);
    CHECK(ws.Modules().size() == 1);
    CHECK_FALSE(ws.RemoveModule("stub"));
}

TEST_CASE("Workspace.OpenNonAsciiPath") {
    TempFile tmp(std::filesystem::path(u8"onyx_ws_ünï_日本.bin"), "payload");
    REQUIRE(std::filesystem::exists(tmp.path));
    Workspace ws;
    REQUIRE(ws.AddModule(std::make_unique<StubModule>(StubOptions{"stub", 90, 2})));
    const DocumentId id = ws.Open(tmp.path);
    REQUIRE(id != 0);
    Document* doc = ws.Get(id);
    REQUIRE(doc);
    CHECK(doc->path == tmp.path);
    CHECK(doc->roots.size() == 2);
    CHECK_FALSE(doc->diags.HasErrors());
}

TEST_CASE("Workspace.ContainerContextCarriesPathReaderAndRootFile") {
    TempFile tmp("onyx_ws_ctx.bin", "12345678");
    Workspace ws;
    auto mod = std::make_unique<CaptureModule>();
    CaptureModule* raw = mod.get();
    REQUIRE(ws.AddModule(std::move(mod)));
    REQUIRE(ws.Open(tmp.path) != 0);
    CHECK(raw->sawParse);
    CHECK(raw->seenPath == tmp.path);
    CHECK(raw->seenSize == 8);
    CHECK(raw->seenFiles == 1);
}

TEST_CASE("Workspace.MountedFilesystemReachesTheModule") {
    TempFile tmp("onyx_ws_mount.BIN", "x");
    Workspace ws;
    auto mod = std::make_unique<MountModule>();
    MountModule* raw = mod.get();
    REQUIRE(ws.AddModule(std::move(mod)));

    const DocumentId id = ws.Open(tmp.path);
    REQUIRE(id != 0);
    CHECK(raw->sawMounted);
    CHECK(ws.Get(id)->mounted);

    raw->refuse = true;
    const DocumentId refused = ws.Open(tmp.path);
    REQUIRE(refused != 0);
    CHECK_FALSE(raw->sawMounted);
    CHECK(ws.Get(refused)->diags.Count(Services::Severity::Warning) == 1);
}

TEST_CASE("Workspace.OpenAsyncDeliversTreeReady") {
    TempFile tmp("onyx_ws_async.bin", "data");
    Workspace ws;
    REQUIRE(ws.AddModule(std::make_unique<StubModule>(StubOptions{"stub", 90, 2})));

    DocumentId ready = 0;
    bool ok = false;
    auto sub = ws.Events().On<TreeReady>([&](const TreeReady& e) {
        ready = e.id;
        ok = e.ok;
    });
    const DocumentId id = ws.OpenAsync(tmp.path);
    REQUIRE(id != 0);
    REQUIRE(PumpUntil(ws, [&] { return ready != 0; }));
    CHECK(ready == id);
    CHECK(ok);
    CHECK(ws.Get(id)->roots.size() == 2);
    CHECK(ws.Get(id)->ready);
}

TEST_CASE("Workspace.CloseReportsAndForgets") {
    TempFile tmp("onyx_ws_close.bin", "data");
    Workspace ws;
    REQUIRE(ws.AddModule(std::make_unique<StubModule>()));
    const DocumentId id = ws.Open(tmp.path);
    REQUIRE(id != 0);

    DocumentId closed = 0;
    auto sub = ws.Events().On<DocumentClosed>([&](const DocumentClosed& e) { closed = e.id; });
    ws.Close(id);
    ws.Pump();
    CHECK(closed == id);
    CHECK(ws.Get(id) == nullptr);
}

TEST_CASE("Workspace.CloseDuringAsyncParseKeepsTheDocumentAliveForTheWorker") {
    TempFile tmp("onyx_ws_uaf.bin", "data");
    std::atomic<bool> started{false}, release{false}, finished{false};
    {
        Workspace ws;
        auto mod = std::make_unique<GatedModule>();
        mod->started = &started;
        mod->release = &release;
        mod->finished = &finished;
        REQUIRE(ws.AddModule(std::move(mod)));

        DocumentId readyFor = 0;
        auto sub = ws.Events().On<TreeReady>([&](const TreeReady& e) { readyFor = e.id; });
        const DocumentId id = ws.OpenAsync(tmp.path);
        REQUIRE(id != 0);
        REQUIRE(PumpUntil(ws, [&] { return started.load(); }));

        ws.Close(id);
        CHECK(ws.Get(id) == nullptr);
        release.store(true);
        REQUIRE(PumpUntil(ws, [&] { return finished.load(); }));
        for (int i = 0; i < 50; ++i) {
            ws.Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(readyFor != id);
    }
}

TEST_CASE("Workspace.RemoveModuleDuringAsyncParseKeepsTheModuleAlive") {
    TempFile tmp("onyx_ws_rm_async.bin", "data");
    std::atomic<bool> started{false}, release{false}, finished{false};
    Workspace ws;
    auto mod = std::make_unique<GatedModule>();
    mod->started = &started;
    mod->release = &release;
    mod->finished = &finished;
    REQUIRE(ws.AddModule(std::move(mod)));
    REQUIRE(ws.OpenAsync(tmp.path) != 0);
    REQUIRE(PumpUntil(ws, [&] { return started.load(); }));

    CHECK(ws.RemoveModule("gated"));
    release.store(true);
    CHECK(PumpUntil(ws, [&] { return finished.load(); }));
}

TEST_CASE("Workspace.ThrowingParseIsContained") {
    TempFile tmp("onyx_ws_throw.bin", "data");
    Workspace ws;
    REQUIRE(ws.AddModule(std::make_unique<StubModule>(StubOptions{"stub", 90, 1, true})));

    DocumentId ready = 0;
    bool ok = true;
    auto sub = ws.Events().On<TreeReady>([&](const TreeReady& e) {
        ready = e.id;
        ok = e.ok;
    });
    DocumentId id = 0;
    CHECK_NOTHROW(id = ws.Open(tmp.path));
    REQUIRE(id != 0);
    ws.Pump();
    CHECK(ready == id);
    CHECK_FALSE(ok);
    CHECK(ws.Get(id)->diags.HasErrors());
    CHECK(ws.Get(id)->ready);
}

TEST_CASE("Workspace.UnreadableFileWithHintYieldsAnErrorDocument") {
    Workspace ws;
    REQUIRE(ws.AddModule(std::make_unique<StubModule>(StubOptions{"stub", 90, 1})));
    const DocumentId id = ws.Open(std::filesystem::temp_directory_path() / "onyx_ws_missing_file.bin", "stub");
    REQUIRE(id != 0);
    Document* doc = ws.Get(id);
    CHECK(doc->ready);
    CHECK(doc->diags.HasErrors());
    CHECK(doc->roots.empty());
}

TEST_CASE("Workspace.CancelOpenCancelsABlockedParse") {
    TempFile tmp("onyx_ws_cancel.bin", "data");
    std::atomic<bool> started{false};
    Workspace ws;
    auto mod = std::make_unique<CancelModule>();
    mod->started = &started;
    REQUIRE(ws.AddModule(std::move(mod)));

    DocumentId ready = 0;
    bool ok = true;
    auto sub = ws.Events().On<TreeReady>([&](const TreeReady& e) {
        ready = e.id;
        ok = e.ok;
    });
    const DocumentId id = ws.OpenAsync(tmp.path);
    REQUIRE(id != 0);
    REQUIRE(PumpUntil(ws, [&] { return started.load(); }));
    CHECK(ws.CancelOpen(id));
    CHECK_FALSE(ws.CancelOpen(999999));
    REQUIRE(PumpUntil(ws, [&] { return ready != 0; }));
    CHECK(ready == id);
    CHECK_FALSE(ok);
}

TEST_CASE("Workspace.SettingsPathLoadsAndLocksAfterTheFirstOpen") {
    const auto settingsFile = std::filesystem::temp_directory_path() / "onyx_ws_settings.toml";
    TempFile tmp("onyx_ws_settings.bin", "data");
    std::error_code ec;
    std::filesystem::remove(settingsFile, ec);
    {
        Workspace ws;
        REQUIRE(ws.AddModule(std::make_unique<StubModule>()));
        REQUIRE(ws.SetWorkspaceSettingsPath(settingsFile));
        CHECK(ws.Settings().GetBool("stub.flag") == true);
        ws.Settings().Set("stub.dir", std::string("cache/tex"));
        REQUIRE(ws.Settings().Save());

        REQUIRE(ws.Open(tmp.path) != 0);
        const auto late = ws.SetWorkspaceSettingsPath(settingsFile);
        REQUIRE_FALSE(late.ok());
        CHECK(late.error().code == "workspace.settingsLocked");
    }
    auto reloaded = Services::Settings::Load(settingsFile);
    CHECK(reloaded.GetString("stub.dir") == "cache/tex");
    std::filesystem::remove(settingsFile, ec);
}

TEST_CASE("Workspace.OptionsSettingsPathIsLoaded") {
    const auto settingsFile = std::filesystem::temp_directory_path() / "onyx_ws_options.toml";
    {
        auto s = Services::Settings::Load(settingsFile);
        s.Set("stub.flag", false);
        REQUIRE(s.Save());
    }
    {
        WorkspaceOptions options;
        options.workerThreads = 1;
        options.settingsPath = settingsFile;
        Workspace ws(options);
        REQUIRE(ws.AddModule(std::make_unique<StubModule>()));
        CHECK(ws.Settings().GetBool("stub.flag") == false);
    }
    std::error_code ec;
    std::filesystem::remove(settingsFile, ec);
}
