#pragma once
#include <Onyx/Viewers/IDocumentContent.h>
#include <cstdint>
#include <vector>
#include <memory>

namespace Onyx::Viewers {

class DocumentWindow {
public:
    DocumentWindow();
    ~DocumentWindow();

    // `docId` associates the tab with the Modules::DocumentId it was
    // opened from (Task 13a) -- CloseTabsForDocument(docId) later closes
    // every tab sharing that value. DocumentWindow deliberately stays
    // decoupled from Onyx::Modules (matches IDocumentContent.h) and just
    // carries the raw uint64_t; 0 is Modules::DocumentId's own reserved
    // "invalid" value (Workspace.h: "0 = invalid"), so a tab added with
    // the default docId (e.g. one not backed by any Workspace document,
    // like MinimalViewer's hex-dump tab) is never matched by
    // CloseTabsForDocument(0).
    void AddTab(std::shared_ptr<IDocumentContent> tab, uint64_t docId = 0);
    void Draw();
    void CloseAll();
    void CloseActiveTab();

    // Closes (deferred to the next Draw(), same one-frame GL/Vulkan grace
    // period as CloseAll()/CloseActiveTab() -- see m_pendingDelete's own
    // comment below) every currently open tab whose docId (recorded by
    // AddTab) equals `docId`. A no-op for docId == 0 (never matches: 0 is
    // the reserved "invalid" DocumentId, so a tab legitimately carries it
    // only when it was never associated with a document to begin with).
    // Restores the legacy close-on-document-close behavior the M3b ledger
    // tracked as an inert regression once AssetDatabase/EventWadClosed
    // were retired (see this file's .cpp for the wiring that calls this
    // from a Workspace DocumentClosed subscription).
    void CloseTabsForDocument(uint64_t docId);

    // Closes exactly the tab whose IDocumentContent pointer equals `tab`
    // (deferred, same grace period). No-op if `tab` is null or is not
    // currently an open tab -- e.g. the user already closed it, or
    // CloseTabsForDocument beat this to it. Used by the async-decode
    // placeholder path (Task 13b): OpenSelection's opener.closePlaceholder
    // callback closes the specific "Decoding..." tab it opened earlier,
    // by identity, once the decode job's Done callback runs.
    void CloseTab(const IDocumentContent* tab);

    // T10: immediately destroys every open tab (and anything still in
    // CloseAll()'s one-frame m_pendingDelete grace period) -- unlike
    // CloseAll(), which defers actual destruction to the NEXT Draw() so a
    // GL/Vulkan resource a just-submitted ImGui draw list still references
    // survives until that submission completes, this runs no such Draw()
    // and does not wait for one. Only safe to call once the caller has
    // already otherwise guaranteed no more frames will be drawn AND the
    // GPU has gone idle (Window::~Window() calls this after its own
    // vkDeviceWaitIdle, before tearing down the VkContext/ImGui backend
    // any tab's own Vulkan resources -- Viewport3D/ImageViewer/VideoPlayer
    // TexturePool instances -- still need alive to unwind cleanly). Not
    // calling this before the VkContext goes away is exactly the shutdown-
    // order gap those classes' own destructors otherwise have to leak
    // around (see Include/Onyx/App/TexturePool.h's class doc comment).
    void Shutdown();

    std::shared_ptr<IDocumentContent> GetActiveDocument() const;
    bool HasActiveDocument() const;

    // Pure-logic accessors (Task 13a tests; also handy for a future
    // "N tabs" status readout) -- neither touches ImGui, unlike Draw().
    size_t TabCount() const { return m_tabs.size(); }
    size_t TabCountForDocument(uint64_t docId) const;

private:
    struct Tab {
        uint64_t                          docId = 0;
        std::shared_ptr<IDocumentContent> content;
    };

    std::vector<Tab> m_tabs;
    int m_activeTabIndex = -1; // Track the currently active tab

    // Tabs queued for destruction. We can't release a viewer's shared_ptr in
    // the middle of the ImGui frame: ImGui::Image() may already have queued
    // the viewer's FBO texture ID into the current draw list. If the dtor
    // runs now (Viewport3D ~ glDeleteTextures), the bound GL texture is freed
    // before ImGui::Render submits it — the GPU then samples whatever is
    // still bound (typically the font atlas), producing a one-frame flash of
    // stretched glyphs. We carry the strong references for one extra frame
    // so the draw call completes before the resources go away.
    std::vector<std::shared_ptr<IDocumentContent>> m_pendingDelete;
};

} // namespace Onyx::Viewers
