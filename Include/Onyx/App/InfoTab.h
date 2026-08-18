#pragma once
#include <Onyx/Services/AssetDatabase.h>
#include <Onyx/Modules/Selection.h>
#include <Onyx/Services/EventBus.h>

namespace Onyx::App {

// Renders one selected entry's metadata plus whatever diags name it.
// Embeddable -- neither Draw() overload opens its own ImGui::Begin/End; a
// host window draws InfoTab into its own content region.
//
// Two independent legs (M3b Task 5):
//   - Draw(AssetDatabase&, AssetEntry*): the profile-era model, driven by
//     whatever caller already holds a selected AssetEntry*. Untouched by
//     this task -- see InfoTab.cpp's file comment for why it currently
//     has no caller in this tree, and Task 6 for its actual fate once the
//     layer it reads from is deleted.
//   - Draw(): the Workspace-bus model. Subscribes to SelectionChanged on
//     construction and re-Resolve()s the last-selected NodePath every
//     frame it draws -- paths go stale when their document closes or
//     reparses to a smaller tree, so re-resolving (rather than caching an
//     AssetEntry*) is what keeps this safe to call indefinitely.
class InfoTab {
public:
    explicit InfoTab(Onyx::Modules::Workspace& workspace);

    void Draw(Onyx::Services::AssetDatabase& db, AssetEntry* e);
    void Draw();

private:
    Onyx::Modules::Workspace&    m_workspace;
    Onyx::Modules::DocumentId    m_selDoc = 0;
    Onyx::Modules::NodePath      m_selPath;
    bool                         m_hasSelection = false;
    Onyx::Services::Subscription m_selectionSub;
};

} // namespace Onyx::App
