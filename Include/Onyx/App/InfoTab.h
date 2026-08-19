#pragma once
#include <Onyx/Modules/Selection.h>
#include <Onyx/Services/EventBus.h>

namespace Onyx::App {

// Renders one selected entry's metadata plus whatever diags name it.
// Draw() does not open its own ImGui::Begin/End; a host window (see
// InspectorPanel, M3b Task 6) draws InfoTab into its own content region.
//
// The Workspace-bus model (M3b Task 5): subscribes to SelectionChanged on
// construction and re-Resolve()s the last-selected NodePath every frame it
// draws -- paths go stale when their document closes or reparses to a
// smaller tree, so re-resolving (rather than caching an AssetEntry*) is
// what keeps this safe to call indefinitely.
//
// The profile-era Draw(AssetDatabase&, AssetEntry*) overload that used to
// live alongside this one was removed in M3b Task 6 along with
// AssetDatabase itself -- it never had a caller in this tree (see the prior
// revision's file comment).
class InfoTab {
public:
    explicit InfoTab(Onyx::Modules::Workspace& workspace);

    void Draw();

private:
    Onyx::Modules::Workspace&    m_workspace;
    Onyx::Modules::DocumentId    m_selDoc = 0;
    Onyx::Modules::NodePath      m_selPath;
    bool                         m_hasSelection = false;
    Onyx::Services::Subscription m_selectionSub;
};

} // namespace Onyx::App
