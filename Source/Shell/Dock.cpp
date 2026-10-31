#include "Onyx/Shell/Dock.h"

#include <imgui_internal.h>

#include <utility>

namespace Onyx::Shell {

namespace {

std::string WindowName(const Ui::IPanel& panel) {
    return std::string(panel.Title()) + "##" + std::string(panel.Id());
}

struct Placement {
    ImGuiDir side;
    float ratio;
    ImGuiID node;
};

void DockPanel(Ui::PanelHost& panels, const std::string& panelId, ImGuiID node) {
    if (const Ui::IPanel* panel = panels.Find(panelId)) {
        ImGui::DockBuilderDockWindow(WindowName(*panel).c_str(), node);
    }
}

}

void ApplyDockLayout(ImGuiID dockspace, const DockLayout& layout, Ui::PanelHost& panels) {
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->WorkSize);

    ImGuiID remaining = dockspace;
    std::vector<Placement> placed;
    std::vector<const DockSlot*> centre;
    for (const DockSlot& slot : layout.slots) {
        if (slot.side == ImGuiDir_None) {
            centre.push_back(&slot);
            continue;
        }
        ImGuiID node = 0;
        for (const Placement& known : placed) {
            if (known.side == slot.side && known.ratio == slot.ratio) {
                node = known.node;
                break;
            }
        }
        if (node == 0) {
            ImGuiID split = 0;
            ImGuiID other = 0;
            ImGui::DockBuilderSplitNode(remaining, slot.side, slot.ratio, &split, &other);
            node = split;
            remaining = other;
            placed.push_back({slot.side, slot.ratio, node});
        }
        DockPanel(panels, slot.panelId, node);
    }
    DockPanel(panels, layout.centerPanelId, remaining);
    for (const DockSlot* slot : centre) {
        DockPanel(panels, slot->panelId, remaining);
    }
    ImGui::DockBuilderFinish(dockspace);
}

DockLayout DefaultLayout() {
    DockLayout layout;
    layout.slots = {
        {"onyx.browser", ImGuiDir_Left, 0.22f},
        {"onyx.inspector", ImGuiDir_Right, 0.25f},
        {"onyx.problems", ImGuiDir_Down, 0.25f},
        {"onyx.log", ImGuiDir_Down, 0.25f},
        {"onyx.transport", ImGuiDir_Down, 0.15f},
        {"onyx.timeline", ImGuiDir_Down, 0.15f},
        {"onyx.viewport", ImGuiDir_None, 0.0f},
        {"onyx.settings", ImGuiDir_None, 0.0f},
    };
    layout.centerPanelId = "onyx.documents";
    return layout;
}

}
