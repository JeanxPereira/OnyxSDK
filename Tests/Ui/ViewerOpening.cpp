#include <doctest/doctest.h>

#include <Onyx/Ui/Viewers/HexView.h>
#include <Onyx/Ui/Viewers/ImageView.h>
#include <Onyx/Ui/Viewers/Opening.h>
#include <Onyx/Ui/Viewers/StockViewers.h>
#include <Onyx/Ui/Viewers/TableView.h>
#include <Onyx/Ui/Viewers/TextView.h>

#include "Support/ObxViewerFixture.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Onyx;
using namespace Onyx::Ui;
using TestSupport::ObxViewerFixture;

namespace {

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
    ObxViewerFixture fx;
    UiContext ui = fx.Make();
    Placed placed;

    const auto opened = OpenEntry(ui, fx.Pick(ObxViewerFixture::kText), placed.Callback());
    REQUIRE_MESSAGE(opened.ok(), opened.error().code << ": " << opened.error().message);
    CHECK(placed.calls == 0);

    REQUIRE(fx.PumpUntil([&] { return placed.calls > 0; }));
    CHECK(placed.calls == 1);
    auto* view = dynamic_cast<TextView*>(placed.view.get());
    REQUIRE(view != nullptr);
    CHECK(view->Lines() == 3);
    CHECK(view->Document() == fx.doc);
    CHECK(view->Node().indices == std::vector<uint32_t>{ObxViewerFixture::kText});
    CHECK(view->Title() == "txt");
}

TEST_CASE("OpenEntry places an ImageView for an image entry") {
    ObxViewerFixture fx;
    UiContext ui = fx.Make();
    Placed placed;

    REQUIRE(OpenEntry(ui, fx.Pick(ObxViewerFixture::kImage), placed.Callback()).ok());
    REQUIRE(fx.PumpUntil([&] { return placed.calls > 0; }));
    auto* view = dynamic_cast<ImageView*>(placed.view.get());
    REQUIRE(view != nullptr);
    CHECK(view->Texture().width == 8);
    CHECK_FALSE(view->Uploaded());
}

TEST_CASE("OpenEntry places a HexView over a blob that reads its rows from the file") {
    ObxViewerFixture fx;
    UiContext ui = fx.Make();
    Placed placed;

    REQUIRE(OpenEntry(ui, fx.Pick(ObxViewerFixture::kBlob), placed.Callback()).ok());
    REQUIRE(fx.PumpUntil([&] { return placed.calls > 0; }));
    auto* view = dynamic_cast<HexView*>(placed.view.get());
    REQUIRE(view != nullptr);
    CHECK(view->Size() == ObxViewerFixture::kBlobSize);
    CHECK(view->RowCount() == (ObxViewerFixture::kBlobSize + 15) / 16);

    std::string expected;
    for (size_t i = 16; i < 32; ++i) {
        char buf[4];
        std::snprintf(buf, sizeof buf, "%02X", static_cast<unsigned>(ObxViewerFixture::BlobByte(i)));
        if (!expected.empty()) expected += ' ';
        expected += buf;
    }
    const HexRow row = view->Row(1);
    CHECK(row.hex == expected);
    CHECK(row.offset == 16);
    CHECK(row.ascii.size() == 16);
}

TEST_CASE("OpenEntry places a TableView with the fixture's columns") {
    ObxViewerFixture fx;
    UiContext ui = fx.Make();
    Placed placed;

    REQUIRE(OpenEntry(ui, fx.Pick(ObxViewerFixture::kTable), placed.Callback()).ok());
    REQUIRE(fx.PumpUntil([&] { return placed.calls > 0; }));
    auto* view = dynamic_cast<TableView*>(placed.view.get());
    REQUIRE(view != nullptr);
    CHECK(view->Columns() == std::vector<std::string>{"col0", "col1"});
    CHECK(view->RowCount() == 3);
}

TEST_CASE("OpenEntry reports ui.viewer.none when nothing routes the type") {
    ObxViewerFixture fx;
    UiContext ui = fx.Make();
    Placed placed;

    const auto opened = OpenEntry(ui, fx.Pick(ObxViewerFixture::kMesh), placed.Callback());
    REQUIRE_FALSE(opened.ok());
    CHECK(opened.error().code == "ui.viewer.none");
    fx.workspace.Jobs().Pump();
    CHECK(placed.calls == 0);
}

TEST_CASE("OpenEntry rejects a selection that names no entry") {
    ObxViewerFixture fx;
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
    ObxViewerFixture fx;
    Modules::Document* document = fx.workspace.Get(fx.doc);
    REQUIRE(document != nullptr);
    fx.workspace.Decoders().Text(document->roots[ObxViewerFixture::kText].type,
                                 [](Modules::DecodeContext&) { return std::optional<Domain::Text>(); });
    UiContext ui = fx.Make();
    Placed placed;

    REQUIRE(OpenEntry(ui, fx.Pick(ObxViewerFixture::kText), placed.Callback()).ok());
    const size_t before = document->diags.Count();
    REQUIRE(fx.PumpUntil([&] { return document->diags.Count() > before; }));
    CHECK(placed.calls == 0);
    bool reported = false;
    for (const Services::Diag& d : document->diags.Snapshot()) reported = reported || d.code == "ui.open.decode";
    CHECK(reported);
}
