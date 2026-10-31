#include <doctest/doctest.h>

#include <Onyx/Core/Modules/Selection.h>
#include <Onyx/Ui/Panels/DocumentBrowser.h>
#include <Onyx/Ui/Panels/Inspector.h>
#include <Onyx/Ui/Panels/LogPanel.h>
#include <Onyx/Ui/Panels/Problems.h>
#include <Onyx/Ui/Panels/SettingsPanel.h>

#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include "HeadlessImGui.h"
#include "Support/ObxWorkspace.h"

using namespace Onyx;
using namespace Onyx::Ui;

TEST_CASE("DocumentBrowser draws one row per visible node") {
    ObxUi fx("browser_rows");
    UiContext ui = fx.Make();
    DocumentBrowser browser;

    browser.Refresh(ui);
    CHECK(browser.VisibleRows() == 1 + 5 + 8);

    browser.SetExpanded(ui, fx.obx.doc, {}, false);
    browser.Refresh(ui);
    CHECK(browser.VisibleRows() == 1);

    browser.SetExpanded(ui, fx.obx.doc, {}, true);
    browser.Refresh(ui);
    CHECK(browser.VisibleRows() == 14);

    HeadlessImGui rig;
    DrawFrames(rig, 3, [&] { browser.Draw(ui); });
    CHECK(browser.RowsDrawn() == 14);
}

TEST_CASE("DocumentBrowser labels a non-ASCII file name in UTF-8") {
    const std::string name = "caf\xC3\xA9-\xC3\xBC.obx";
    ObxUi fx("browser_utf8", 8, name);
    UiContext ui = fx.Make();
    DocumentBrowser browser;

    browser.Refresh(ui);
    REQUIRE(browser.VisibleRows() == 14);
    CHECK(browser.LabelOf(0) == name);

    HeadlessImGui rig;
    bool visible = false;
    DrawFrames(rig, 3, [&] {
        browser.Draw(ui);
        visible = !ImGui::GetCurrentWindow()->SkipItems;
    });
    CHECK(visible);
    CHECK(browser.RowsDrawn() == 14);
}

TEST_CASE("DocumentBrowser selecting posts SelectionChanged with the node path") {
    ObxUi fx("browser_select");
    UiContext ui = fx.Make();
    DocumentBrowser browser;
    browser.Refresh(ui);

    std::optional<Modules::SelectionChanged> seen;
    const auto sub = fx.obx.workspace.Events().On<Modules::SelectionChanged>(
        [&](const Modules::SelectionChanged& e) { seen = e; });

    browser.SelectRow(ui, 3);
    fx.obx.workspace.Pump();

    REQUIRE(seen.has_value());
    CHECK(seen->doc == fx.obx.doc);
    CHECK(seen->path.indices == std::vector<uint32_t>{2});
    CHECK(browser.Selection().Current().node.indices == std::vector<uint32_t>{2});

    browser.SelectRow(ui, 0);
    fx.obx.workspace.Pump();
    CHECK(seen->path.indices.empty());
    CHECK(seen->doc == fx.obx.doc);
}

TEST_CASE("DocumentBrowser activation calls onOpen with the selection") {
    ObxUi fx("browser_open");
    UiContext ui = fx.Make();
    DocumentBrowser browser;
    browser.Refresh(ui);

    browser.Activate(ui, 2);
    CHECK(browser.Selection().Current().node.indices == std::vector<uint32_t>{1});

    int calls = 0;
    Selection received;
    browser.onOpen = [&](UiContext&, const Selection& s) {
        ++calls;
        received = s;
    };
    browser.Activate(ui, 2);
    CHECK(calls == 1);
    CHECK(received.doc == fx.obx.doc);
    CHECK(received.node.indices == std::vector<uint32_t>{1});
}

TEST_CASE("DocumentBrowser closing a document clears rows and selection") {
    ObxUi fx("browser_close");
    UiContext ui = fx.Make();
    DocumentBrowser browser;
    browser.Refresh(ui);
    browser.SelectRow(ui, 2);

    browser.RequestClose(fx.obx.doc);
    browser.Refresh(ui);
    CHECK(browser.VisibleRows() == 0);
    CHECK(browser.Selection().Current().Empty());
}

TEST_CASE("DocumentBrowser draws a bounded number of rows for 20000 children") {
    ObxUi fx("browser_flat", 20000);
    UiContext ui = fx.Make();
    DocumentBrowser browser;

    HeadlessImGui rig;
    const auto frameMs = [&] {
        const auto start = std::chrono::steady_clock::now();
        DrawFrames(rig, 1, [&] { browser.Draw(ui); });
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };

    // The first frame flattens the 20006 rows; later frames only clip and draw. The bound is the
    // best of three so a loaded machine does not fail a test about algorithmic cost.
    CHECK(frameMs() < 1000.0);
    CHECK(browser.VisibleRows() == 1 + 5 + 20000);
    double best = 1e9;
    for (int i = 0; i < 3; ++i) best = (std::min)(best, frameMs());
    CHECK(best < 100.0);

    CHECK(browser.RowsDrawn() > 0);
    CHECK(browser.RowsDrawn() < 200);
}

TEST_CASE("The workspace panels draw together without asserts") {
    ObxUi fx("browser_all");
    UiContext ui = fx.Make();
    fx.panels.Add(std::make_unique<DocumentBrowser>());
    fx.panels.Add(std::make_unique<Inspector>());
    fx.panels.Add(std::make_unique<Problems>());
    fx.panels.Add(std::make_unique<LogPanel>(Services::Log::Default()));
    fx.panels.Add(std::make_unique<SettingsPanel>());

    auto* browser = dynamic_cast<DocumentBrowser*>(fx.panels.Find("onyx.browser"));
    REQUIRE(browser);
    browser->Refresh(ui);
    browser->SelectRow(ui, 1);

    fx.obx.workspace.Get(fx.obx.doc)->diags.Report({Services::Severity::Warning, "t.warn", "careful", std::nullopt});

    HeadlessImGui rig;
    for (int f = 0; f < 3; ++f) {
        rig.NewFrame();
        fx.panels.DrawAll(ui);
        rig.EndFrame();
    }
    for (const char* id : {"onyx.browser", "onyx.inspector", "onyx.problems", "onyx.log", "onyx.settings"}) {
        IPanel* panel = fx.panels.Find(id);
        REQUIRE(panel);
        CHECK(panel->visible);
        const std::string name = std::string(panel->Title()) + "##" + id;
        const ImGuiWindow* window = ImGui::FindWindowByName(name.c_str());
        REQUIRE(window);
        CHECK(window->DrawList->CmdBuffer.Size > 0);
        CHECK(window->DrawList->IdxBuffer.Size > 0);
    }
}

TEST_CASE("DocumentBrowser badge follows diagnostics reported after the tree is ready") {
    ObxUi fx("browser_badge");
    UiContext ui = fx.Make();
    DocumentBrowser browser;
    browser.Refresh(ui);
    CHECK(browser.BadgeOf(0).empty());

    fx.obx.workspace.Get(fx.obx.doc)->diags.Report({Services::Severity::Error, "t.late", "late", std::nullopt});
    HeadlessImGui rig;
    DrawFrames(rig, 1, [&] { browser.Draw(ui); });
    CHECK(browser.BadgeOf(0) == "1");
}

TEST_CASE("DocumentBrowser drops the selection of a document closed elsewhere") {
    ObxUi fx("browser_external_close");
    UiContext ui = fx.Make();
    DocumentBrowser browser;
    browser.Refresh(ui);
    browser.SelectRow(ui, 2);
    fx.obx.workspace.Pump();

    std::optional<Modules::SelectionChanged> seen;
    const auto sub = fx.obx.workspace.Events().On<Modules::SelectionChanged>(
        [&](const Modules::SelectionChanged& e) { seen = e; });

    fx.obx.workspace.Close(fx.obx.doc);
    browser.Refresh(ui);
    fx.obx.workspace.Pump();

    CHECK(browser.VisibleRows() == 0);
    CHECK(browser.Selection().Current().Empty());
    REQUIRE(seen.has_value());
    CHECK(seen->doc == 0);
    CHECK(seen->path.indices.empty());
}
