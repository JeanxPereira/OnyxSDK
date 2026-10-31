#pragma once

#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/Core/Services/EventBus.h>
#include <Onyx/Ui/Panel.h>

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace Onyx::Ui {

// A tab bar of document views. A closed tab is destroyed at the second Draw after it was retired, so
// closing from inside a view's own Draw is safe, and GPU resources that a panel recorded into the
// frame of the retirement survive that frame's submit however early in the frame the tab was closed.
// Tabs of a document close when the workspace pumps DocumentClosed; the workspace must outlive this
// panel.
class DocumentTabs : public IPanel {
public:
    explicit DocumentTabs(Modules::Workspace& workspace);

    DocumentTabs(const DocumentTabs&) = delete;
    DocumentTabs& operator=(const DocumentTabs&) = delete;

    std::string_view Id() const override { return "onyx.documents"; }
    std::string_view Title() const override { return "Viewer"; }
    void Draw(UiContext& ui) override;

    // The new tab becomes the active one.
    void Add(std::unique_ptr<IDocumentView> view);
    void CloseActive();
    void CloseAll();
    void CloseForDocument(Modules::DocumentId doc);

    IDocumentView* Active();
    size_t Count() const { return m_tabs.size(); }

private:
    void Retire(size_t index);

    std::vector<std::unique_ptr<IDocumentView>> m_tabs;
    std::vector<std::unique_ptr<IDocumentView>> m_retired;
    std::vector<std::unique_ptr<IDocumentView>> m_dying;
    size_t m_active = 0;
    bool m_select = false;
    Services::Subscription m_closed;
};

}
