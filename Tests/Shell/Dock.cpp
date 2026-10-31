#include <doctest/doctest.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <string>

#include "../Ui/Support/HeadlessImGui.h"
#include "Onyx/Shell/Dock.h"
#include "Onyx/Shell/Shell.h"
#include "Support/ShellSupport.h"

using namespace Onyx;
using Onyx::TestSupport::TempDir;
using Onyx::TestSupport::TestOptions;

namespace {

const char* const kStockPanels[] = {"onyx.browser",  "onyx.documents", "onyx.inspector", "onyx.problems", "onyx.log",
                                    "onyx.settings", "onyx.viewport",  "onyx.transport", "onyx.timeline"};

}

TEST_CASE("DefaultLayout names every stock panel exactly once") {
    const Shell::DockLayout layout = Shell::DefaultLayout();
    std::map<std::string, int> seen;
    for (const auto& slot : layout.slots) {
        ++seen[slot.panelId];
    }
    ++seen[layout.centerPanelId];
    CHECK(layout.centerPanelId == "onyx.documents");
    for (const char* id : kStockPanels) {
        CHECK_MESSAGE(seen[id] == 1, id);
    }
    CHECK(seen.size() == std::size(kStockPanels));
}

TEST_CASE("ApplyDockLayout docks every panel window and tabs the shared slots together") {
    TempDir dir("dock");
    Shell::Shell shell(TestOptions(dir.path));
    HeadlessImGui rig;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    rig.NewFrame();
    const ImGuiID dockspace = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    Shell::ApplyDockLayout(dockspace, Shell::DefaultLayout(), shell.Panels());

    const auto dockOf = [&](const char* panelId) {
        const Ui::IPanel* panel = shell.Panels().Find(panelId);
        REQUIRE(panel != nullptr);
        const std::string name = std::string(panel->Title()) + "##" + std::string(panel->Id());
        const ImGuiWindowSettings* settings = ImGui::FindWindowSettingsByID(ImHashStr(name.c_str()));
        REQUIRE_MESSAGE(settings != nullptr, panelId);
        return settings->DockId;
    };
    for (const char* id : kStockPanels) {
        CHECK_MESSAGE(dockOf(id) != 0, id);
    }
    CHECK(dockOf("onyx.problems") == dockOf("onyx.log"));
    CHECK(dockOf("onyx.browser") != dockOf("onyx.inspector"));
    CHECK(dockOf("onyx.documents") != dockOf("onyx.browser"));
    CHECK(dockOf("onyx.documents") != dockOf("onyx.problems"));
    rig.EndFrame();
}
