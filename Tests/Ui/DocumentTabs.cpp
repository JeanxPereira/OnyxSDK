#include <doctest/doctest.h>

#include <Onyx/Ui/Viewers/DocumentTabs.h>
#include <Onyx/Ui/Viewers/TextView.h>

#include "Support/ObxViewerFixture.h"

#include <memory>

using namespace Onyx;
using namespace Onyx::Ui;
using TestSupport::ObxViewerFixture;

namespace {

std::unique_ptr<IDocumentView> MakeView(Modules::DocumentId doc, const char* name) {
    return std::make_unique<TextView>(doc, Modules::NodePath{{0}}, name, Domain::Text{"a\nb", ""});
}

}

TEST_CASE("DocumentTabs adds, activates and closes tabs") {
    ObxViewerFixture fx;
    DocumentTabs tabs(fx.workspace);
    CHECK(tabs.Id() == "onyx.documents");
    CHECK(tabs.Title() == "Viewer");
    CHECK(tabs.Active() == nullptr);

    tabs.Add(MakeView(1, "one"));
    tabs.Add(MakeView(1, "two"));
    tabs.Add(MakeView(2, "three"));
    REQUIRE(tabs.Count() == 3);
    REQUIRE(tabs.Active() != nullptr);
    CHECK(tabs.Active()->Title() == "three");

    tabs.CloseActive();
    CHECK(tabs.Count() == 2);
    CHECK(tabs.Active()->Title() == "two");

    tabs.CloseAll();
    CHECK(tabs.Count() == 0);
    CHECK(tabs.Active() == nullptr);
    tabs.CloseActive();
    CHECK(tabs.Count() == 0);
}

TEST_CASE("DocumentTabs closes the tabs of one document") {
    ObxViewerFixture fx;
    DocumentTabs tabs(fx.workspace);
    tabs.Add(MakeView(1, "one"));
    tabs.Add(MakeView(2, "two"));
    tabs.Add(MakeView(1, "three"));

    tabs.CloseForDocument(1);
    REQUIRE(tabs.Count() == 1);
    CHECK(tabs.Active()->Title() == "two");
}

TEST_CASE("DocumentTabs drops tabs when the workspace closes their document") {
    ObxViewerFixture fx;
    DocumentTabs tabs(fx.workspace);
    tabs.Add(MakeView(fx.doc, "mine"));
    tabs.Add(MakeView(fx.doc + 1, "other"));

    fx.workspace.Close(fx.doc);
    fx.workspace.Pump();
    REQUIRE(tabs.Count() == 1);
    CHECK(tabs.Active()->Title() == "other");
}

TEST_CASE("DocumentTabs draws and destroys a closed tab after the frame") {
    ObxViewerFixture fx;
    HeadlessImGui imgui;
    UiContext ui = fx.Make();
    DocumentTabs tabs(fx.workspace);
    tabs.Add(MakeView(1, "one"));
    tabs.Add(MakeView(1, "two"));

    TestSupport::DrawFrames(imgui, tabs, ui);
    CHECK(tabs.Count() == 2);
    tabs.CloseActive();
    TestSupport::DrawFrames(imgui, tabs, ui);
    REQUIRE(tabs.Count() == 1);
    CHECK(tabs.Active()->Title() == "one");
}
