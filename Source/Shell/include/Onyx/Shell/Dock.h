#pragma once

#include <imgui.h>

#include <string>
#include <vector>

#include "Onyx/Ui/PanelHost.h"

namespace Onyx::Shell {

// One panel docked to a side of the space left over by the slots before it. Slots with the same side
// and ratio share one node and appear as tabs. ImGuiDir_None tabs the panel into the centre node.
struct DockSlot {
    std::string panelId;
    ImGuiDir side = ImGuiDir_None;
    float ratio = 0.25f;
};

struct DockLayout {
    std::vector<DockSlot> slots;
    std::string centerPanelId;
};

// Rebuilds the dockspace from `layout`. Panels the host does not hold are skipped.
void ApplyDockLayout(ImGuiID dockspace, const DockLayout& layout, Ui::PanelHost& panels);

// Browser left 0.22, inspector right 0.25, problems and log tabbed at the bottom 0.25, transport and
// timeline tabbed under the centre 0.15, the document tabs in the centre with the viewport and the
// settings panel as further centre tabs.
DockLayout DefaultLayout();

}
