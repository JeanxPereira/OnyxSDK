#pragma once
#include <Onyx/App/IPanel.h>
#include <Onyx/Modules/Workspace.h>

namespace Onyx::App {

class StatusBar : public IPanel {
public:
    explicit StatusBar(Onyx::Modules::Workspace& workspace);
    int selectedLog = -1;
    void Draw() override;
    std::string_view getName() const override { return "Log"; }
    void SetMessage(const char* msg);

private:
    // Renders the Workspace-bus leg (M3b Task 5): a progress line per
    // still-opening document, or once every open document is ready, an
    // "N docs, M entries, K errors" summary. Deliberately polls
    // m_workspace.Documents() fresh every frame rather than subscribing
    // to DocumentOpened/TreeReady/DocumentClosed -- same reasoning as
    // DocumentBrowser.cpp's file comment: Draw() already re-reads live
    // state every frame, so a subscription would exist only to flip a
    // "please redraw" flag nothing here needs.
    void DrawWorkspaceStatus();

    Onyx::Modules::Workspace& m_workspace;
};

} // namespace Onyx::App
