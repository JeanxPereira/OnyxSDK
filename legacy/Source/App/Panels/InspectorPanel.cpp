#include <Onyx/App/Panels/InspectorPanel.h>
#include "imgui.h"

namespace Onyx::App {

InspectorPanel::InspectorPanel(Onyx::Modules::Workspace& workspace)
    : m_infoTab(workspace) {}

void InspectorPanel::Draw() {
    if (!visible) return;

    ImGui::Begin("Inspector", &visible);
    m_infoTab.Draw();
    ImGui::End();
}

} // namespace Onyx::App
