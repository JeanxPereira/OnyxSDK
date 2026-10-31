#include "Onyx/Ui/Panels/DocumentBrowser.h"

#include <Onyx/Core/Domain/AssetEntry.h>
#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/Ui/Icons.h>
#include <Onyx/Ui/Theme.h>
#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/Widgets.h>

#include <imgui.h>

#include <algorithm>
#include <string>

namespace Onyx::Ui {

namespace {

glm::vec4 FailedColor() {
    const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    const bool dark = (bg.x + bg.y + bg.z) < 1.5f;
    return dark ? glm::vec4(0.95f, 0.38f, 0.36f, 1.0f) : glm::vec4(0.75f, 0.12f, 0.12f, 1.0f);
}

}

void DocumentBrowser::Refresh(UiContext& ui) {
    if (!m_closeQueue.empty()) {
        std::vector<Modules::DocumentId> queue;
        queue.swap(m_closeQueue);
        for (const Modules::DocumentId id : queue) {
            ui.workspace.Close(id);
            std::erase_if(m_expanded, [id](const Key& key) { return key.first == id; });
            m_known.erase(id);
            if (m_selection.Current().doc == id) {
                m_selection.Set({}, ui.workspace.Events());
                m_selectedEntry = nullptr;
            }
        }
        m_dirty = true;
    }

    std::vector<std::pair<Modules::DocumentId, bool>> signature;
    for (const auto& doc : ui.workspace.Documents()) {
        signature.emplace_back(doc->id, doc->ready.load());
    }
    if (!m_dirty && signature == m_signature) return;
    m_signature = std::move(signature);
    m_dirty = false;
    Rebuild(ui);
}

void DocumentBrowser::Rebuild(UiContext& ui) {
    m_rows.clear();
    for (const auto& doc : ui.workspace.Documents()) {
        if (m_known.insert(doc->id).second) m_expanded.insert({doc->id, nullptr});

        const bool ready = doc->ready.load();
        Row row;
        row.doc = doc->id;
        row.docLabel = doc->path.filename().string();
        row.hasChildren = ready && !doc->roots.empty();
        row.expanded = m_expanded.contains({doc->id, nullptr});
        row.glyph = Icon(Types::IconId::Container);
        if (ready) {
            if (const size_t problems = doc->diags.Count(); problems > 0) {
                row.badge = std::to_string(problems);
            }
        }
        const uint32_t rowIndex = static_cast<uint32_t>(m_rows.size());
        const bool expanded = row.expanded;
        m_rows.push_back(std::move(row));
        if (ready && expanded) Append(ui, doc->id, doc->roots, 1, rowIndex);
    }

    const Onyx::Ui::Selection& current = m_selection.Current();
    m_selectedEntry = current.node.indices.empty() ? nullptr : EntryFor(ui, current);
}

void DocumentBrowser::Append(UiContext& ui, Modules::DocumentId doc,
                             const std::vector<Domain::AssetEntry>& entries, uint32_t depth,
                             uint32_t parentRow) {
    Types::TypeId lastType;
    const Types::TypeSpec* spec = nullptr;
    for (uint32_t i = 0; i < entries.size(); ++i) {
        const Domain::AssetEntry& entry = entries[i];
        if (!spec || entry.type != lastType) {
            lastType = entry.type;
            spec = &ui.workspace.Catalog().Spec(entry.type);
        }
        Row row;
        row.doc = doc;
        row.entry = &entry;
        row.depth = depth;
        row.index = i;
        row.parent = parentRow;
        row.hasChildren = !entry.children.empty();
        row.expanded = row.hasChildren && m_expanded.contains({doc, &entry});
        row.failed = Domain::HasFlag(entry.flags, Domain::NodeFlags::Failed);
        row.glyph = Icon(spec->icon != Types::IconId::None ? spec->icon : Domain::DefaultIcon(entry.kind));
        row.color = glm::vec4(spec->color.r, spec->color.g, spec->color.b, spec->color.a);
        const uint32_t rowIndex = static_cast<uint32_t>(m_rows.size());
        const bool expanded = row.expanded;
        m_rows.push_back(std::move(row));
        if (expanded) Append(ui, doc, entry.children, depth + 1, rowIndex);
    }
}

Modules::NodePath DocumentBrowser::PathOf(size_t row) const {
    Modules::NodePath path;
    for (size_t r = row; m_rows[r].entry; r = m_rows[r].parent) {
        path.indices.push_back(m_rows[r].index);
    }
    std::reverse(path.indices.begin(), path.indices.end());
    return path;
}

const Domain::AssetEntry* DocumentBrowser::EntryFor(UiContext& ui, const Onyx::Ui::Selection& selection) const {
    const Modules::Document* doc = ui.workspace.Get(selection.doc);
    if (!doc || !doc->ready.load()) return nullptr;
    return Modules::Resolve(*doc, selection.node);
}

void DocumentBrowser::SetExpanded(UiContext& ui, Modules::DocumentId doc, const Modules::NodePath& path,
                                  bool expanded) {
    const Modules::Document* document = ui.workspace.Get(doc);
    if (!document) return;
    const Domain::AssetEntry* entry = nullptr;
    if (!path.indices.empty()) {
        if (!document->ready.load()) return;
        entry = Modules::Resolve(*document, path);
        if (!entry) return;
    }
    const Key key{doc, entry};
    if (expanded) m_expanded.insert(key);
    else m_expanded.erase(key);
    m_dirty = true;
}

void DocumentBrowser::Select(UiContext& ui, const Onyx::Ui::Selection& selection) {
    const Modules::Document* doc = ui.workspace.Get(selection.doc);
    if (!doc) return;
    if (doc->ready.load()) {
        const std::vector<Domain::AssetEntry>* level = &doc->roots;
        m_expanded.insert({selection.doc, nullptr});
        for (size_t i = 0; i + 1 < selection.node.indices.size(); ++i) {
            const uint32_t index = selection.node.indices[i];
            if (index >= level->size()) break;
            const Domain::AssetEntry& entry = (*level)[index];
            m_expanded.insert({selection.doc, &entry});
            level = &entry.children;
        }
    }
    m_selection.Set(selection, ui.workspace.Events());
    m_selectedEntry = selection.node.indices.empty() ? nullptr : EntryFor(ui, selection);
    m_dirty = true;
}

void DocumentBrowser::SelectRow(UiContext& ui, size_t row) {
    if (row >= m_rows.size()) return;
    m_selection.Set(Onyx::Ui::Selection{m_rows[row].doc, PathOf(row)}, ui.workspace.Events());
    m_selectedEntry = m_rows[row].entry;
}

void DocumentBrowser::Activate(UiContext& ui, size_t row) {
    if (row >= m_rows.size()) return;
    SelectRow(ui, row);
    if (onOpen) onOpen(ui, m_selection.Current());
}

void DocumentBrowser::DrawRow(UiContext& ui, size_t index) {
    const Row& row = m_rows[index];
    ++m_rowsDrawn;

    ImGui::PushID(static_cast<int>(index));
    const float indent = ImGui::GetStyle().IndentSpacing * static_cast<float>(row.depth);
    if (row.depth > 0) ImGui::Indent(indent);

    const Onyx::Ui::Selection& current = m_selection.Current();
    const bool selected = row.doc == current.doc && row.entry == m_selectedEntry;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth |
                               ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (!row.hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
    if (selected) flags |= ImGuiTreeNodeFlags_Selected;
    if (row.hasChildren) ImGui::SetNextItemOpen(row.expanded, ImGuiCond_Always);

    const char* label = row.entry ? row.entry->name.c_str() : row.docLabel.c_str();
    const glm::vec4 color = row.failed ? FailedColor() : row.color;
    Widgets::TreeRow("row", label, row.glyph, color, flags, selected);

    if (row.hasChildren && ImGui::IsItemToggledOpen()) {
        const Key key{row.doc, row.entry};
        if (row.expanded) m_expanded.erase(key);
        else m_expanded.insert(key);
        m_dirty = true;
    } else if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        SelectRow(ui, index);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && onOpen) {
        onOpen(ui, m_selection.Current());
    }

    if (!row.entry && ImGui::BeginPopupContextItem("ctx")) {
        if (ImGui::MenuItem("Close document")) RequestClose(row.doc);
        ImGui::EndPopup();
    }
    if (!row.badge.empty()) {
        ImGui::SameLine();
        Widgets::Hint(row.badge.c_str());
    }

    if (row.depth > 0) ImGui::Unindent(indent);
    ImGui::PopID();
}

void DocumentBrowser::Draw(UiContext& ui) {
    Refresh(ui);
    m_rowsDrawn = 0;
    if (m_rows.empty()) {
        Widgets::Hint("No documents open");
        return;
    }

    const float pad = std::max(ImGui::GetFrameHeight() - ImGui::GetFontSize(), 8.0f) * 0.5f;
    const float rowHeight = ImGui::GetFontSize() + pad * 2.0f + ImGui::GetStyle().ItemSpacing.y;
    Widgets::ForEachVisible(m_rows.size(), rowHeight, [&](size_t i) { DrawRow(ui, i); });

    const Onyx::Ui::Selection& current = m_selection.Current();
    if (onOpen && !current.Empty() && ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        onOpen(ui, current);
    }
}

}
