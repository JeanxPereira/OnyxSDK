#pragma once

#include <Onyx/App/IPanel.h>
#include <Onyx/Modules/Selection.h>

namespace Onyx::App {

// Generic dock panel over the Workspace: draws one tree per open document,
// using only TypeSpec metadata (icon/color via TypeCatalog, label from
// AssetEntry::displayName/name) -- no per-game knowledge, unlike the
// legacy Iso/Pak/Wad browsers this is meant to eventually stand alongside
// for any module that goes through the Workspace path (M3b).
//
// Clicking a node posts Modules::SelectionChanged{docId, path} on the
// Workspace's EventBus; the panel does not track "the" selection itself,
// it only announces it.
//
// Draw() reads Workspace state live, every frame (Documents(), each
// Document's `ready` flag, roots/children) rather than caching a
// snapshot, so there is nothing here that DocumentOpened/TreeReady/
// DocumentClosed would need to invalidate -- the panel deliberately does
// not subscribe to them. See DocumentBrowser.cpp's file comment for the
// full justification.
class DocumentBrowser : public IPanel {
public:
    explicit DocumentBrowser(Onyx::Modules::Workspace& workspace);

    void Draw() override;
    std::string_view getName() const override { return "Documents"; }

private:
    // Recursively draws `entry` and its children, extending `path` with
    // each child index visited so a click always has the exact NodePath
    // that reached the clicked node. `doc` is only threaded through for
    // its id (the event payload) and its document-level diag count
    // (shown on a Failed node's tooltip).
    void DrawEntry(Onyx::Modules::Document& doc, const Onyx::Domain::AssetEntry& entry,
                    Onyx::Modules::NodePath& path);

    Onyx::Modules::Workspace& m_workspace;
};

} // namespace Onyx::App
