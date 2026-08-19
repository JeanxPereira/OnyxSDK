#pragma once
#include <Onyx/App/IPanel.h>
#include <Onyx/App/InfoTab.h>

namespace Onyx::Modules { class Workspace; }

namespace Onyx::App {

// Dock host for InfoTab's Workspace-bus leg (M3b Task 6 -- InfoTab had no
// host panel until this task; see InfoTab.cpp's file comment for the prior
// state). Owns the InfoTab instance, and therefore its RAII
// SelectionChanged subscription, for the panel's lifetime. That lifetime is
// safe against the Workspace's own teardown because it is App's lifetime:
// Window declares m_workspace before m_app (see Window.h's member-order
// comment), so members destruct in reverse -- m_app (and every panel it
// owns, including this one) is torn down BEFORE m_workspace, which is what
// lets m_infoTab unsubscribe from a still-live EventBus in its destructor.
class InspectorPanel : public IPanel {
public:
    explicit InspectorPanel(Onyx::Modules::Workspace& workspace);

    void Draw() override;
    std::string_view getName() const override { return "Inspector"; }

private:
    InfoTab m_infoTab;
};

} // namespace Onyx::App
