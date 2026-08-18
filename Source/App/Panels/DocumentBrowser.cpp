#include <Onyx/App/Panels/DocumentBrowser.h>

// No RAII EventBus subscriptions here on purpose. DocumentOpened/
// TreeReady/DocumentClosed would only ever be used to flip some "please
// redraw" flag, and this panel has no such flag to flip: Draw() already
// re-reads Workspace::Documents() and each Document's live state fresh
// every single frame (panels redraw unconditionally, per IPanel), so a
// document appearing, finishing its parse, or disappearing is reflected
// on the very next frame with zero extra bookkeeping. Subscribing to
// those events would add three RAII members and three lambdas whose only
// job is to do nothing -- pure overhead with no consumer. If a future
// need appears (e.g. auto-expanding a just-opened document's tree, or
// auto-focusing the panel), that is the point to add a subscription for
// specifically that behavior, not preemptively.

#include <Onyx/App/Widgets.h>
#include <Onyx/Services/Diagnostics.h>
#include <Onyx/Types/TypeCatalog.h>
#include "imgui.h"

#include <cstdio>
#include <string>

namespace Onyx::App {

namespace {

// Warning tint for a Failed node -- overrides whatever color the entry's
// registered type carries, the same way a compiler error line ignores
// the file's normal syntax highlighting.
constexpr ImVec4 kFailedColor = ImVec4(1.0f, 0.65f, 0.15f, 1.0f);

} // namespace

DocumentBrowser::DocumentBrowser(Onyx::Modules::Workspace& workspace)
    : m_workspace(workspace) {}

void DocumentBrowser::Draw() {
    if (!visible) return;

    ImGui::Begin("Documents", &visible);

    for (const auto& docPtr : m_workspace.Documents()) {
        Onyx::Modules::Document& doc = *docPtr;
        ImGui::PushID(static_cast<int>(doc.id));

        std::string label = doc.path.filename().string();
        if (label.empty()) label = doc.path.string();

        // Thread rule (Workspace.h): never read a Document before its
        // TreeReady -- a worker thread may still be writing roots/diags
        // for an async open. `ready` is the one field safe to poll.
        if (!doc.ready.load()) {
            ImGui::TextDisabled("%s (loading...)", label.c_str());
            ImGui::PopID();
            continue;
        }

        const bool open = ImGui::TreeNodeEx(
            label.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth);
        if (open) {
            Onyx::Modules::NodePath path;
            for (uint32_t i = 0; i < doc.roots.size(); ++i) {
                path.indices = {i};
                DrawEntry(doc, doc.roots[i], path);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::End();
}

void DocumentBrowser::DrawEntry(Onyx::Modules::Document& doc, const Onyx::Domain::AssetEntry& entry,
                                 Onyx::Modules::NodePath& path) {
    const std::string& label = entry.displayName.empty() ? entry.name : entry.displayName;
    const bool failed = entry.flags == Onyx::Domain::NodeFlags::Failed;

    const char* icon = m_workspace.Catalog().Icon(entry.typeId);
    ImVec4 color;
    if (failed) {
        color = kFailedColor;
    } else {
        float c[4];
        m_workspace.Catalog().Color(entry.typeId, c);
        color = ImVec4(c[0], c[1], c[2], c[3]);
    }

    const bool hasChildren = !entry.children.empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth;
    flags |= hasChildren ? ImGuiTreeNodeFlags_OpenOnArrow
                          : (ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);

    // path.indices.back() is this entry's own index among its siblings --
    // stable and unique within the current ID scope, which is exactly
    // what ColoredTreeNode wants for its "###id" suffix.
    char rowId[16];
    std::snprintf(rowId, sizeof(rowId), "%u", path.indices.back());
    const bool open = Onyx::App::Widgets::ColoredTreeNode(rowId, label.c_str(), icon, color, flags, false);

    if (failed && ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::Text("%zu error diag(s)", doc.diags.Count(Onyx::Services::Severity::Error));
        ImGui::EndTooltip();
    }

    if (ImGui::IsItemClicked()) {
        m_workspace.Events().Post(Onyx::Modules::SelectionChanged{doc.id, path});
    }

    if (hasChildren && open) {
        for (uint32_t i = 0; i < entry.children.size(); ++i) {
            path.indices.push_back(i);
            DrawEntry(doc, entry.children[i], path);
            path.indices.pop_back();
        }
        ImGui::TreePop();
    }
}

} // namespace Onyx::App
