#include <doctest/doctest.h>

#include <Onyx/App/ViewerOpening.h>
#include <Onyx/App/ViewerRouting.h>
#include <Onyx/Modules/DecoderRegistry.h>
#include <Onyx/Modules/Workspace.h>
#include <Onyx/Parsers/SceneNode.h>
#include <Onyx/Parsers/TextureData.h>
#include <Onyx/Types/TypeId.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <thread>

using namespace Onyx::App;
using namespace Onyx::Modules;
using namespace Onyx::Types;
using namespace Onyx::Parsers;

namespace {

struct FakeMesh : SceneData {};
struct FakeTexture : TextureData {};

void RegisterCapabilities(DecoderRegistry& reg, TypeId type, bool scene, bool image, bool text) {
    if (scene) {
        reg.Scene(type, [](DecodeContext&) { return std::make_unique<FakeMesh>(); });
    }
    if (image) {
        reg.Image(type, [](DecodeContext&) { return std::make_unique<FakeTexture>(); });
    }
    if (text) {
        reg.Text(type, [](DecodeContext&) { return std::make_optional(DecodedText{"hello box", ""}); });
    }
}

std::filesystem::path write_temp_file(const std::string& name, const std::string& contents) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream f(path, std::ios::binary);
    f << contents;
    f.close();
    return path;
}

// Fixture for OpenSelection (the App/ViewerOpening.h seam): a document
// with one Text entry, one Image entry, one entry of a type no decoder
// is registered for, and one Failed entry -- enough to exercise every
// branch OpenSelection has (decode+open, no-capability, Failed-skips).
struct RoutingFake : Onyx::Modules::IGameModule {
    Onyx::Types::TypeId textType;
    Onyx::Types::TypeId imageType;
    Onyx::Types::TypeId blobType;    // deliberately no decoder registered
    Onyx::Types::TypeId corruptType; // has a Text decoder, but it salvage-fails

    ModuleInfo Info() const override { return ModuleInfo{"routingfake", "RoutingFake", {}, {}}; }

    ProbeResult Probe(const ProbeInput& in) const override {
        if (!in.header.empty() && in.header[0] == std::byte{'R'}) return ProbeResult{90, "'R' magic"};
        return ProbeResult{0, "no magic"};
    }

    void RegisterTypes(Onyx::Types::TypeRegistrar& r) override {
        Onyx::Types::TypeInfo text;
        text.key = "text";
        text.label = "Text";
        textType = r.Add(text);

        Onyx::Types::TypeInfo image;
        image.key = "image";
        image.label = "Image";
        imageType = r.Add(image);

        Onyx::Types::TypeInfo blob;
        blob.key = "blob";
        blob.label = "Blob";
        blobType = r.Add(blob);

        Onyx::Types::TypeInfo corrupt;
        corrupt.key = "corrupt";
        corrupt.label = "Corrupt";
        corruptType = r.Add(corrupt);
    }

    void RegisterDecoders(DecoderRegistry& reg) override {
        reg.Text(textType, [](DecodeContext&) { return std::make_optional(DecodedText{"hello box", ""}); });
        reg.Image(imageType, [](DecodeContext&) { return std::make_unique<FakeTexture>(); });
        // blobType intentionally left with no decoder.
        // corruptType HAS a Text decoder, but it always salvage-fails
        // (returns nullopt) -- the "capability exists, decode still
        // fails" case, distinct from blobType's "no capability at all".
        reg.Text(corruptType, [](DecodeContext&) { return std::nullopt; });
    }

    ParseResult ParseContainer(ContainerContext& ctx) override {
        Onyx::Domain::AssetEntry textEntry;
        textEntry.name = "hello.txt";
        textEntry.typeId = textType;
        ctx.roots.push_back(textEntry); // roots[0]

        Onyx::Domain::AssetEntry imageEntry;
        imageEntry.name = "gradient.img";
        imageEntry.typeId = imageType;
        ctx.roots.push_back(imageEntry); // roots[1]

        Onyx::Domain::AssetEntry blobEntry;
        blobEntry.name = "raw.blob";
        blobEntry.typeId = blobType;
        ctx.roots.push_back(blobEntry); // roots[2]

        Onyx::Domain::AssetEntry failedEntry;
        failedEntry.name = "broken.txt";
        failedEntry.typeId = textType;
        failedEntry.flags = Onyx::Domain::NodeFlags::Failed;
        ctx.diags.Report(Onyx::Services::Diag{
            Onyx::Services::Severity::Error, "routingfake.entry.bad", "entry 3 corrupt", std::nullopt});
        ctx.roots.push_back(failedEntry); // roots[3]

        Onyx::Domain::AssetEntry corruptEntry;
        corruptEntry.name = "corrupt.txt";
        corruptEntry.typeId = corruptType;
        ctx.roots.push_back(corruptEntry); // roots[4] -- capability exists, Decode* salvage-fails

        return ParseResult{true};
    }
};

// Fixture for Task 13b (async decode): a single Text entry whose decoder
// blocks -- signals `started`, then loops until either `release` is set
// (normal completion path) or ctx.progress.CancelRequested() goes true
// (the cooperative cancel OpenSelection wires up via its own internal
// DocumentClosed subscription) -- mirroring workspace_test.cpp's
// SlowFake/CancelFake fixtures, but for a decode instead of a parse.
// `decodeThreadId` records which thread actually ran the decoder, so a
// test can assert it is never the caller's own thread.
struct BlockingTextFake : Onyx::Modules::IGameModule {
    Onyx::Types::TypeId textType;
    std::atomic<bool>*   started       = nullptr;
    std::atomic<bool>*   release       = nullptr;
    std::atomic<bool>*   finished      = nullptr;
    std::thread::id*     decodeThreadId = nullptr;

    ModuleInfo Info() const override { return ModuleInfo{"blockingfake", "BlockingFake", {}, {}}; }

    ProbeResult Probe(const ProbeInput& in) const override {
        if (!in.header.empty() && in.header[0] == std::byte{'K'}) return ProbeResult{90, "'K' magic"};
        return ProbeResult{0, "no magic"};
    }

    void RegisterTypes(Onyx::Types::TypeRegistrar& r) override {
        Onyx::Types::TypeInfo text;
        text.key = "text";
        text.label = "Text";
        textType = r.Add(text);
    }

    void RegisterDecoders(DecoderRegistry& reg) override {
        reg.Text(textType, [this](DecodeContext& ctx) -> std::optional<DecodedText> {
            if (decodeThreadId) *decodeThreadId = std::this_thread::get_id();
            if (started) started->store(true);
            while (!release->load() && !ctx.progress.CancelRequested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            const bool cancelled = ctx.progress.CancelRequested();
            if (finished) finished->store(true);
            if (cancelled) return std::nullopt;
            return std::make_optional(DecodedText{"decoded", ""});
        });
    }

    ParseResult ParseContainer(ContainerContext& ctx) override {
        Onyx::Domain::AssetEntry textEntry;
        textEntry.name = "block.txt";
        textEntry.typeId = textType;
        ctx.roots.push_back(textEntry); // roots[0]
        return ParseResult{true};
    }
};

} // namespace

TEST_CASE("RouteForType: all 8 capability permutations, priority Scene > Image > Text") {
    // mask bit 2 = Scene, bit 1 = Image, bit 0 = Text -- every combination
    // of the three capabilities being registered or not, including none
    // (mask 0, which must route to None) and all three at once (mask 7,
    // which must still prefer Scene).
    for (uint32_t mask = 0; mask < 8; ++mask) {
        const bool scene = (mask & 0b100) != 0;
        const bool image = (mask & 0b010) != 0;
        const bool text  = (mask & 0b001) != 0;

        DecoderRegistry reg;
        // TypeId 0 is reserved for Unknown/invalid -- offset so every
        // permutation gets a distinct, valid type.
        TypeId type{mask + 1};
        RegisterCapabilities(reg, type, scene, image, text);

        ViewerKind expected = ViewerKind::None;
        if (scene)      expected = ViewerKind::Scene;
        else if (image) expected = ViewerKind::Image;
        else if (text)  expected = ViewerKind::Text;

        CAPTURE(mask);
        CAPTURE(scene);
        CAPTURE(image);
        CAPTURE(text);
        CHECK(RouteForType(reg, type) == expected);
    }
}

TEST_CASE("RouteForType: unregistered TypeId routes to None") {
    DecoderRegistry reg;
    reg.Scene(TypeId{1}, [](DecodeContext&) { return std::make_unique<FakeMesh>(); });

    TypeId unregistered{999};
    CHECK(RouteForType(reg, unregistered) == ViewerKind::None);
}

TEST_CASE("RouteForType: invalid (default) TypeId routes to None") {
    DecoderRegistry reg;
    // Register every capability under some other type -- the invalid
    // TypeId{} (value 0) must never accidentally alias one of them.
    RegisterCapabilities(reg, TypeId{1}, true, true, true);

    TypeId invalid{};
    CHECK_FALSE(invalid.valid());
    CHECK(RouteForType(reg, invalid) == ViewerKind::None);
}

// ── ViewerOpening: OpenSelection (Include/Onyx/App/ViewerOpening.h) ───────
// The seam behind the double-click glue: routes synchronously, decodes on
// a JobQueue worker thread (Task 13b), and hands the result to an injected
// ViewerOpener, without ever touching ImGui or a real viewer class --
// these tests supply recording callbacks instead, pumping ws.Jobs() to
// observe the async completion. Titled "ViewerOpening: ..." rather than
// "OpenSelection: ..." so
// the OnyxSelection ctest filter (`*Selection:*`, Tests/CMakeLists.txt),
// which targets Tests/selection_test.cpp's "Selection: ..." cases, does
// not also pick these up by substring match -- OnyxViewerRouting is what
// runs these instead (see its own filter below).

TEST_CASE("ViewerOpening: Text entry decodes and invokes openText") {
    auto tmp = write_temp_file("onyx_openselection_text.bin", "R____");
    Onyx::Modules::DocumentId id = 0;
    {
        Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
        ws.AddModule(std::make_unique<RoutingFake>());

        id = ws.Open(tmp);
        REQUIRE(id != 0);
        REQUIRE(ws.Get(id));
        REQUIRE(ws.Get(id)->roots.size() == 5);

        bool opened = false;
        std::string openedName, openedText;
        ViewerOpener opener;
        opener.openText = [&](DocumentId, std::string name, DecodedText text) {
            opened = true;
            openedName = name;
            openedText = text.text;
        };

        ViewerKind kind = OpenSelection(ws, SelectionChanged{id, NodePath{{0}}}, opener);
        CHECK(kind == ViewerKind::Text);

        // Decode now runs on the "decode" job lane (Task 13b) -- Done only
        // fires once this test Pump()s it, same as any other async job.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!opened && std::chrono::steady_clock::now() < deadline) {
            ws.Jobs().Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        CHECK(opened);
        CHECK(openedName == "hello.txt");
        CHECK(openedText == "hello box");
    } // ~Workspace closes the Document's OsFile, releasing the OS handle
      // (Windows refuses to remove an open file) -- see workspace_test.cpp.

    std::filesystem::remove(tmp);
}

TEST_CASE("ViewerOpening: Image entry decodes and invokes openImage") {
    auto tmp = write_temp_file("onyx_openselection_image.bin", "R____");
    {
        Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
        ws.AddModule(std::make_unique<RoutingFake>());

        auto id = ws.Open(tmp);
        REQUIRE(id != 0);

        bool opened = false;
        std::string openedName;
        ViewerOpener opener;
        opener.openImage = [&](DocumentId, std::string name, std::unique_ptr<TextureData> tex) {
            opened = true;
            openedName = name;
            CHECK(tex != nullptr);
        };

        ViewerKind kind = OpenSelection(ws, SelectionChanged{id, NodePath{{1}}}, opener);
        CHECK(kind == ViewerKind::Image);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!opened && std::chrono::steady_clock::now() < deadline) {
            ws.Jobs().Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        CHECK(opened);
        CHECK(openedName == "gradient.img");
    }

    std::filesystem::remove(tmp);
}

TEST_CASE("ViewerOpening: entry with no decoder capability routes to None, opener untouched") {
    auto tmp = write_temp_file("onyx_openselection_none.bin", "R____");
    {
        Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
        ws.AddModule(std::make_unique<RoutingFake>());

        auto id = ws.Open(tmp);
        REQUIRE(id != 0);

        bool anyOpened = false;
        ViewerOpener opener;
        opener.openText  = [&](DocumentId, std::string, DecodedText) { anyOpened = true; };
        opener.openImage = [&](DocumentId, std::string, std::unique_ptr<TextureData>) { anyOpened = true; };
        opener.openScene = [&](DocumentId, std::string, std::unique_ptr<SceneData>) { anyOpened = true; };

        ViewerKind kind = OpenSelection(ws, SelectionChanged{id, NodePath{{2}}}, opener);

        CHECK(kind == ViewerKind::None);
        CHECK_FALSE(anyOpened);

        // roots[4] is the opposite miss: corruptType DOES have a Text
        // decoder registered (RoutingFake::RegisterDecoders), but that
        // decoder always salvage-fails (returns nullopt) -- distinct
        // from roots[2]'s "no capability at all" above. Routing (Task 13b:
        // now synchronous and separate from the decode itself) sees the
        // capability and returns Text -- the salvage failure only shows up
        // once the async decode job actually runs; OpenSelection must
        // still never call the opener for it (it used to fail silently
        // too; it still also logs via ONYX_LOGF_WARN from the Done callback now,
        // which this seam-level test has no cheap way to capture/assert).
        anyOpened = false;
        kind = OpenSelection(ws, SelectionChanged{id, NodePath{{4}}}, opener);
        CHECK(kind == ViewerKind::Text);

        // Let the (salvage-failing) decode job run and its Done callback
        // fire via Pump() -- give it a beat, since there is no "opened"
        // flag to poll for a decode that is SUPPOSED to never open
        // anything; PendingCallbacks() draining to 0 is the observable
        // proxy for "Done has run".
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (ws.Jobs().PendingCallbacks() == 0 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(ws.Jobs().PendingCallbacks() > 0);
        ws.Jobs().Pump();

        CHECK_FALSE(anyOpened);
    }

    std::filesystem::remove(tmp);
}

TEST_CASE("ViewerOpening: Failed entry is never decoded, opener untouched") {
    auto tmp = write_temp_file("onyx_openselection_failed.bin", "R____");
    {
        Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
        ws.AddModule(std::make_unique<RoutingFake>());

        auto id = ws.Open(tmp);
        REQUIRE(id != 0);

        bool anyOpened = false;
        ViewerOpener opener;
        opener.openText = [&](DocumentId, std::string, DecodedText) { anyOpened = true; };

        // roots[3] is the Failed entry -- same type (textType) as
        // roots[0], which DOES have a Text decoder, proving the Failed
        // check runs before routing/decoding, not just when no
        // capability exists.
        ViewerKind kind = OpenSelection(ws, SelectionChanged{id, NodePath{{3}}}, opener);

        CHECK(kind == ViewerKind::None);
        CHECK_FALSE(anyOpened);
    }

    std::filesystem::remove(tmp);
}

TEST_CASE("ViewerOpening: a stale/unresolvable path routes to None") {
    auto tmp = write_temp_file("onyx_openselection_stale.bin", "R____");
    {
        Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
        ws.AddModule(std::make_unique<RoutingFake>());

        auto id = ws.Open(tmp);
        REQUIRE(id != 0);

        ViewerOpener opener; // no callbacks set -- must never be needed
        ViewerKind kind = OpenSelection(ws, SelectionChanged{id, NodePath{{99}}}, opener);
        CHECK(kind == ViewerKind::None);

        // An unknown DocumentId (e.g. the document was Closed) also
        // routes to None rather than crashing.
        kind = OpenSelection(ws, SelectionChanged{id + 12345, NodePath{{0}}}, opener);
        CHECK(kind == ViewerKind::None);
    }

    std::filesystem::remove(tmp);
}

// ── Task 13b: async decode ─────────────────────────────────────────────

TEST_CASE("ViewerOpening: decode runs off the caller thread, placeholder brackets it, "
          "completion opens the viewer on Pump") {
    auto tmp = write_temp_file("onyx_openselection_async.bin", "K____");
    {
        Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());

        std::atomic<bool> started{false}, release{false}, finished{false};
        std::thread::id decodeThreadId{};
        auto mod = std::make_unique<BlockingTextFake>();
        mod->started = &started;
        mod->release = &release;
        mod->finished = &finished;
        mod->decodeThreadId = &decodeThreadId;
        ws.AddModule(std::move(mod));

        auto id = ws.Open(tmp);
        REQUIRE(id != 0);

        bool opened = false, placeholderOpened = false, placeholderClosed = false;
        DocumentId placeholderDoc = 0, openedDoc = 0;
        std::string placeholderName, openedText;
        ViewerOpener opener;
        opener.openPlaceholder = [&](DocumentId doc, std::string name) -> std::shared_ptr<void> {
            placeholderOpened = true;
            placeholderDoc = doc;
            placeholderName = name;
            return std::make_shared<int>(1); // any non-null opaque handle
        };
        opener.closePlaceholder = [&](std::shared_ptr<void>) { placeholderClosed = true; };
        opener.openText = [&](DocumentId doc, std::string, DecodedText text) {
            opened = true;
            openedDoc = doc;
            openedText = text.text;
        };

        ViewerKind kind = OpenSelection(ws, SelectionChanged{id, NodePath{{0}}}, opener);
        CHECK(kind == ViewerKind::Text);

        // openPlaceholder runs synchronously, on THIS thread, before
        // OpenSelection returns -- no waiting needed for it.
        CHECK(placeholderOpened);
        CHECK(placeholderDoc == id);
        CHECK(placeholderName == "block.txt");
        // Nothing has been Pump()'d yet, so Done cannot have run.
        CHECK_FALSE(opened);
        CHECK_FALSE(placeholderClosed);

        // Deterministic hand-off: wait for the worker to actually be
        // inside the decode.
        auto deadline1 = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!started.load() && std::chrono::steady_clock::now() < deadline1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(started.load());

        // The decode ran on some JobQueue worker thread, never on this
        // (the caller's) thread.
        CHECK(decodeThreadId != std::this_thread::get_id());
        CHECK(decodeThreadId != std::thread::id{});

        release.store(true);

        auto deadline2 = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!opened && std::chrono::steady_clock::now() < deadline2) {
            ws.Jobs().Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        CHECK(opened);
        CHECK(openedDoc == id);
        CHECK(openedText == "decoded");
        CHECK(placeholderClosed); // closePlaceholder always runs, success or not
    } // ~Workspace closes the Document's OsFile, releasing the OS handle.

    std::filesystem::remove(tmp);
}

TEST_CASE("ViewerOpening: closing the document cancels the in-flight decode; opener never runs") {
    auto tmp = write_temp_file("onyx_openselection_cancel.bin", "K____");
    {
        Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());

        std::atomic<bool> started{false}, release{false}, finished{false};
        auto mod = std::make_unique<BlockingTextFake>();
        mod->started = &started;
        mod->release = &release;
        mod->finished = &finished;
        ws.AddModule(std::move(mod));

        auto id = ws.Open(tmp);
        REQUIRE(id != 0);

        bool opened = false, placeholderClosed = false;
        ViewerOpener opener;
        opener.openPlaceholder = [](DocumentId, std::string) -> std::shared_ptr<void> {
            return std::make_shared<int>(1);
        };
        opener.closePlaceholder = [&](std::shared_ptr<void>) { placeholderClosed = true; };
        opener.openText = [&](DocumentId, std::string, DecodedText) { opened = true; };

        ViewerKind kind = OpenSelection(ws, SelectionChanged{id, NodePath{{0}}}, opener);
        CHECK(kind == ViewerKind::Text);

        // Deterministic hand-off #1: wait for the worker to be inside the
        // decode, blocked in BlockingTextFake's loop.
        auto deadline1 = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!started.load() && std::chrono::steady_clock::now() < deadline1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(started.load());

        // Close while the decode is still blocked -- posts DocumentClosed
        // (not dispatched until Events().Pump(), same as every other
        // Workspace event).
        ws.Close(id);
        CHECK(ws.Get(id) == nullptr);

        // Dispatch it: OpenSelection's own internal DocumentClosed
        // subscription observes it and calls the submitted job's
        // JobHandle::Cancel() -- the exact cooperative flag
        // BlockingTextFake's decoder polls via
        // ctx.progress.CancelRequested().
        ws.Events().Pump();

        // Deterministic hand-off #2: the decoder must notice the cancel
        // and return ON ITS OWN -- `release` is deliberately never set in
        // this test, so if the decoder ever returns it can only be
        // because it saw the cancel.
        auto deadline2 = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!finished.load() && std::chrono::steady_clock::now() < deadline2) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(finished.load());
        CHECK_FALSE(release.load());

        // Drain both queues -- if the opener wrongly ran despite the
        // cancel, this is where it would have happened.
        auto deadline3 = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (std::chrono::steady_clock::now() < deadline3) {
            ws.Jobs().Pump();
            ws.Events().Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        CHECK_FALSE(opened);
        CHECK(placeholderClosed); // Done still ran (and closed the
                                   // placeholder) -- it just never opened
                                   // the real viewer, since Get(docId) is
                                   // null by the time it checks.
    } // ~Workspace joins the worker (already finished) before removing tmp.

    std::filesystem::remove(tmp);
}
