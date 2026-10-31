#include <doctest/doctest.h>

#include <Onyx/Ui/Viewers/HexView.h>
#include <Onyx/Ui/Viewers/ImageView.h>
#include <Onyx/Ui/Viewers/Opening.h>
#include <Onyx/Ui/Viewers/StockViewers.h>
#include <Onyx/Ui/Viewers/TableView.h>
#include <Onyx/Ui/Viewers/TextView.h>

#include "Support/ObxWorkspace.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace Onyx;
using namespace Onyx::Ui;

namespace {

// Waits, without pumping, until a worker has queued a continuation; false after ten seconds.
bool WaitForCallback(Modules::Workspace& workspace) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (workspace.Jobs().PendingCallbacks() == 0) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

struct Placed {
    std::unique_ptr<IDocumentView> view;
    int calls = 0;

    std::function<void(std::unique_ptr<IDocumentView>)> Callback() {
        return [this](std::unique_ptr<IDocumentView> v) {
            view = std::move(v);
            ++calls;
        };
    }
};

}

TEST_CASE("RegisterStockViewers registers the five stock viewers") {
    ViewerRegistry registry;
    RegisterStockViewers(registry);
    std::vector<std::string> ids;
    for (const ViewerSpec& spec : registry.All()) ids.push_back(spec.id);
    CHECK(ids == std::vector<std::string>{"onyx.image", "onyx.text", "onyx.hex", "onyx.table", "onyx.audio"});
}

TEST_CASE("OpenEntry decodes a text entry on the job lane and places a TextView after Pump") {
    ObxUi fx;
    UiContext ui = fx.Make();
    Placed placed;

    const auto opened = OpenEntry(ui, fx.Pick(ObxWorkspace::kText), placed.Callback());
    REQUIRE_MESSAGE(opened.ok(), opened.error().code << ": " << opened.error().message);
    CHECK(placed.calls == 0);

    REQUIRE(fx.PumpUntil([&] { return placed.calls > 0; }));
    CHECK(placed.calls == 1);
    auto* view = dynamic_cast<TextView*>(placed.view.get());
    REQUIRE(view != nullptr);
    CHECK(view->Lines() == 3);
    CHECK(view->Document() == fx.doc);
    CHECK(view->Node().indices == std::vector<uint32_t>{ObxWorkspace::kText});
    CHECK(view->Title() == "txt");
}

TEST_CASE("OpenEntry places an ImageView for an image entry") {
    ObxUi fx;
    UiContext ui = fx.Make();
    Placed placed;

    REQUIRE(OpenEntry(ui, fx.Pick(ObxWorkspace::kImage), placed.Callback()).ok());
    REQUIRE(fx.PumpUntil([&] { return placed.calls > 0; }));
    auto* view = dynamic_cast<ImageView*>(placed.view.get());
    REQUIRE(view != nullptr);
    CHECK(view->Texture().width == 8);
    CHECK_FALSE(view->Uploaded());
}

TEST_CASE("OpenEntry places a HexView over a blob that reads its rows from the file") {
    ObxUi fx;
    UiContext ui = fx.Make();
    Placed placed;

    REQUIRE(OpenEntry(ui, fx.Pick(ObxWorkspace::kBlob), placed.Callback()).ok());
    REQUIRE(fx.PumpUntil([&] { return placed.calls > 0; }));
    auto* view = dynamic_cast<HexView*>(placed.view.get());
    REQUIRE(view != nullptr);
    CHECK(view->Size() == ObxWorkspace::kBlobSize);
    CHECK(view->RowCount() == (ObxWorkspace::kBlobSize + 15) / 16);

    std::string expected;
    for (size_t i = 16; i < 32; ++i) {
        char buf[4];
        std::snprintf(buf, sizeof buf, "%02X", static_cast<unsigned>(ObxWorkspace::BlobByte(i)));
        if (!expected.empty()) expected += ' ';
        expected += buf;
    }
    const HexRow row = view->Row(1);
    CHECK(row.hex == expected);
    CHECK(row.offset == 16);
    CHECK(row.ascii.size() == 16);
}

TEST_CASE("OpenEntry places a TableView with the fixture's columns") {
    ObxUi fx;
    UiContext ui = fx.Make();
    Placed placed;

    REQUIRE(OpenEntry(ui, fx.Pick(ObxWorkspace::kTable), placed.Callback()).ok());
    REQUIRE(fx.PumpUntil([&] { return placed.calls > 0; }));
    auto* view = dynamic_cast<TableView*>(placed.view.get());
    REQUIRE(view != nullptr);
    CHECK(view->Columns() == std::vector<std::string>{"col0", "col1"});
    CHECK(view->RowCount() == 3);
}

TEST_CASE("OpenEntry reports ui.viewer.none when nothing routes the type") {
    ObxUi fx;
    UiContext ui = fx.Make();
    Placed placed;

    const auto opened = OpenEntry(ui, fx.Pick(ObxWorkspace::kMesh), placed.Callback());
    REQUIRE_FALSE(opened.ok());
    CHECK(opened.error().code == "ui.viewer.none");
    fx.workspace.Jobs().Pump();
    CHECK(placed.calls == 0);
}

TEST_CASE("OpenEntry rejects a selection that names no entry") {
    ObxUi fx;
    UiContext ui = fx.Make();
    Placed placed;

    Selection missingDoc = fx.Pick(0);
    missingDoc.doc = fx.doc + 100;
    CHECK(OpenEntry(ui, missingDoc, placed.Callback()).error().code == "ui.open.entry");

    Selection missingNode = fx.Pick(99);
    CHECK(OpenEntry(ui, missingNode, placed.Callback()).error().code == "ui.open.entry");
    CHECK(OpenEntry(ui, Selection{}, placed.Callback()).error().code == "ui.open.entry");
    CHECK(placed.calls == 0);
}

TEST_CASE("OpenEntry reports a failed decode on the document and places nothing") {
    ObxUi fx;
    Modules::Document* document = fx.workspace.Get(fx.doc);
    REQUIRE(document != nullptr);
    fx.workspace.Decoders().Text(document->roots[ObxWorkspace::kText].type,
                                 [](Modules::DecodeContext&) { return std::optional<Domain::Text>(); });
    UiContext ui = fx.Make();
    Placed placed;

    REQUIRE(OpenEntry(ui, fx.Pick(ObxWorkspace::kText), placed.Callback()).ok());
    const size_t before = document->diags.Count();
    REQUIRE(fx.PumpUntil([&] { return document->diags.Count() > before; }));
    CHECK(placed.calls == 0);
    bool reported = false;
    for (const Services::Diag& d : document->diags.Snapshot()) reported = reported || d.code == "ui.open.decode";
    CHECK(reported);
}

TEST_CASE("OpenEntry lets the last reference to a closed document die inside Pump") {
    ObxUi fx;
    UiContext ui = fx.Make();
    Placed placed;
    std::weak_ptr<Modules::Document> watch;
    for (const auto& d : fx.workspace.Documents()) {
        if (d->id == fx.doc) watch = d;
    }
    REQUIRE_FALSE(watch.expired());

    REQUIRE(OpenEntry(ui, fx.Pick(ObxWorkspace::kText), placed.Callback()).ok());
    // The worker has finished and dropped its closure; only the continuation's result holds the document.
    REQUIRE(WaitForCallback(fx.workspace));
    fx.workspace.Close(fx.doc);
    CHECK_FALSE(watch.expired());
    fx.workspace.Jobs().Pump();
    CHECK(placed.calls == 0);
    CHECK(watch.expired());
}

TEST_CASE("OpenEntry on a document closed before the worker starts places nothing") {
    ObxUi fx;
    UiContext ui = fx.Make();
    Placed placed;

    REQUIRE(OpenEntry(ui, fx.Pick(ObxWorkspace::kText), placed.Callback()).ok());
    fx.workspace.Close(fx.doc);
    REQUIRE(WaitForCallback(fx.workspace));
    fx.workspace.Jobs().Pump();
    CHECK(placed.calls == 0);
}

TEST_CASE("OpenEntry hands a bare entry its file and range") {
    ObxUi fx;
    UiContext ui = fx.Make();
    const Domain::AssetEntry* entry = Modules::Resolve(*fx.workspace.Get(fx.doc), fx.Pick(ObxWorkspace::kBlob).node);
    REQUIRE(entry != nullptr);
    const Domain::ByteRange expected = entry->source;

    ViewerSpec probe;
    probe.id = "probe";
    probe.target = Domain::MediaKind::Raw;
    probe.priority = 100;
    bool seen = false;
    probe.create = [&](OpenRequest& request) {
        seen = true;
        CHECK(request.file != nullptr);
        CHECK(request.range.fileIndex == expected.fileIndex);
        CHECK(request.range.offset == expected.offset);
        CHECK(request.range.size == expected.size);
        CHECK(request.any == nullptr);
        return std::unique_ptr<IDocumentView>();
    };
    ui.viewers.Register(std::move(probe));

    Placed placed;
    REQUIRE(OpenEntry(ui, fx.Pick(ObxWorkspace::kBlob), placed.Callback()).ok());
    CHECK(seen);
}

TEST_CASE("OpenEntry decodes the Any slot for a spec naming its capability") {
    ObxUi fx;
    UiContext ui = fx.Make();
    Modules::Document* document = fx.workspace.Get(fx.doc);
    const Types::TypeId type = document->roots[ObxWorkspace::kBlob].type;
    fx.workspace.Decoders().Any(type, "probe", [](Modules::DecodeContext&) { return std::make_shared<int>(42); });
    fx.workspace.Decoders().Any(type, "empty", [](Modules::DecodeContext&) { return std::shared_ptr<void>(); });

    std::shared_ptr<void> received;
    std::string capability;
    const auto make = [&](const char* wanted) {
        ViewerSpec spec;
        spec.id = "any";
        spec.target = Domain::MediaKind::Raw;
        spec.priority = 100;
        spec.anyCapability = wanted;
        spec.create = [&](OpenRequest& request) {
            received = request.any;
            capability = request.anyCapability;
            return std::unique_ptr<IDocumentView>(
                std::make_unique<TextView>(request.doc, request.node, request.name, Domain::Text{}));
        };
        return spec;
    };

    ui.viewers.Register(make("probe"));
    Placed placed;
    REQUIRE(OpenEntry(ui, fx.Pick(ObxWorkspace::kBlob), placed.Callback()).ok());
    REQUIRE(fx.PumpUntil([&] { return placed.calls > 0; }));
    REQUIRE(received != nullptr);
    CHECK(*std::static_pointer_cast<int>(received) == 42);
    CHECK(capability == "probe");
    CHECK(placed.view != nullptr);

    ui.viewers.Register(make("empty"));
    Placed refused;
    const size_t before = document->diags.Count();
    REQUIRE(OpenEntry(ui, fx.Pick(ObxWorkspace::kBlob), refused.Callback()).ok());
    REQUIRE(fx.PumpUntil([&] { return document->diags.Count() > before; }));
    CHECK(refused.calls == 0);
    bool reported = false;
    for (const Services::Diag& d : document->diags.Snapshot()) reported = reported || d.code == "ui.open.any";
    CHECK(reported);
}

TEST_CASE("OpenEntry on a bare entry naming a missing file fails with ui.open.range") {
    ObxUi fx;
    UiContext ui = fx.Make();
    Modules::Document* document = fx.workspace.Get(fx.doc);
    document->roots[ObxWorkspace::kBlob].source.fileIndex = static_cast<uint32_t>(document->files.size() + 3);

    bool created = false;
    ViewerSpec probe;
    probe.id = "probe";
    probe.target = Domain::MediaKind::Raw;
    probe.priority = 100;
    probe.create = [&](OpenRequest&) {
        created = true;
        return std::unique_ptr<IDocumentView>();
    };
    ui.viewers.Register(std::move(probe));

    Placed placed;
    const auto opened = OpenEntry(ui, fx.Pick(ObxWorkspace::kBlob), placed.Callback());
    REQUIRE_FALSE(opened.ok());
    CHECK(opened.error().code == "ui.open.range");
    CHECK_FALSE(created);
    CHECK(placed.calls == 0);
}

TEST_CASE("OpenEntry routes a Video entry with no decoders to a Video spec with its file and range") {
    ObxUi fx;
    UiContext ui = fx.Make();
    Modules::Document* document = fx.workspace.Get(fx.doc);
    Types::TypeSpec clip;
    clip.key = "clip";
    clip.label = "clip";
    clip.media = Domain::MediaKind::Video;
    document->roots[ObxWorkspace::kBlob].type = fx.workspace.Catalog().Register("test", clip);
    const Domain::ByteRange expected = document->roots[ObxWorkspace::kBlob].source;
    REQUIRE(expected.offset != 0);

    bool seen = false;
    ViewerSpec probe;
    probe.id = "video";
    probe.target = Domain::MediaKind::Video;
    probe.create = [&](OpenRequest& request) {
        seen = true;
        CHECK(request.kind == Domain::MediaKind::Video);
        CHECK(request.file != nullptr);
        CHECK(request.range.fileIndex == expected.fileIndex);
        CHECK(request.range.offset == expected.offset);
        CHECK(request.range.size == expected.size);
        return std::unique_ptr<IDocumentView>();
    };
    ui.viewers.Register(std::move(probe));

    Placed placed;
    const auto opened = OpenEntry(ui, fx.Pick(ObxWorkspace::kBlob), placed.Callback());
    REQUIRE_MESSAGE(opened.ok(), opened.error().code << ": " << opened.error().message);
    CHECK(seen);
}
