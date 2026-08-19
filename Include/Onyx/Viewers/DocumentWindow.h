#pragma once
#include <Onyx/Viewers/IDocumentContent.h>
#include <vector>
#include <memory>

namespace Onyx::Viewers {

class DocumentWindow {
public:
    DocumentWindow();
    ~DocumentWindow();

    void AddTab(std::shared_ptr<IDocumentContent> tab);
    void Draw();
    void CloseAll();
    void CloseActiveTab();

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

private:
    std::vector<std::shared_ptr<IDocumentContent>> m_tabs;
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
