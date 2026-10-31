#include "Onyx/Ui/Panels/Problems.h"

#include <Onyx/Core/Domain/AssetEntry.h>
#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/Ui/Icons.h>
#include <Onyx/Ui/Panels/DocumentBrowser.h>
#include <Onyx/Ui/PanelHost.h>
#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/Widgets.h>

#include <imgui.h>

namespace Onyx::Ui {

namespace {

const char* SeverityGlyph(Services::Severity severity) {
    switch (severity) {
        case Services::Severity::Info: return Icon(Types::IconId::Info);
        case Services::Severity::Warning: return Icon(Types::IconId::Warning);
        case Services::Severity::Error: return Icon(Types::IconId::Error);
    }
    return "";
}

bool Contains(const Domain::ByteRange& range, uint64_t offset) {
    return range.size > 0 && offset >= range.offset && offset - range.offset < range.size;
}

bool Descend(const std::vector<Domain::AssetEntry>& level, uint64_t offset, bool anyFile,
             std::vector<uint32_t>& path) {
    for (uint32_t i = 0; i < level.size(); ++i) {
        const Domain::AssetEntry& entry = level[i];
        path.push_back(i);
        if (Descend(entry.children, offset, anyFile, path)) return true;
        if ((anyFile || entry.source.fileIndex == 0) && Contains(entry.source, offset)) return true;
        path.pop_back();
    }
    return false;
}

}

std::optional<Modules::NodePath> Problems::FindEntry(const Modules::Document& doc, const Services::ByteRef& at) {
    if (!doc.ready.load()) return std::nullopt;
    Modules::NodePath path;
    const bool anyFile = at.file.empty();
    if (!anyFile && at.file != doc.path.string() && at.file != doc.path.filename().string()) return std::nullopt;
    if (!Descend(doc.roots, at.offset, anyFile, path.indices)) return std::nullopt;
    return path;
}

const ProblemsModel& Problems::Model(UiContext& ui) {
    Signature signature;
    signature.mask = (showInfo ? 1u : 0u) | (showWarnings ? 2u : 0u) | (showErrors ? 4u : 0u);
    for (const auto& doc : ui.workspace.Documents()) {
        if (doc->ready.load()) signature.docs.emplace_back(doc->id, doc->diags.Count());
    }
    if (m_built && signature == m_signature) return m_model;

    m_model = {};
    for (const auto& doc : ui.workspace.Documents()) {
        if (!doc->ready.load()) continue;
        for (Services::Diag& diag : doc->diags.Snapshot()) {
            switch (diag.severity) {
                case Services::Severity::Info: ++m_model.info; break;
                case Services::Severity::Warning: ++m_model.warnings; break;
                case Services::Severity::Error: ++m_model.errors; break;
            }
            const bool shown = (diag.severity == Services::Severity::Info && showInfo) ||
                               (diag.severity == Services::Severity::Warning && showWarnings) ||
                               (diag.severity == Services::Severity::Error && showErrors);
            if (!shown) continue;
            ProblemRow row;
            row.doc = doc->id;
            row.glyph = SeverityGlyph(diag.severity);
            row.text = diag.code.empty() ? diag.message : diag.code + "  " + diag.message;
            row.diag = std::move(diag);
            m_model.rows.push_back(std::move(row));
        }
    }
    m_signature = std::move(signature);
    m_built = true;
    return m_model;
}

void Problems::Draw(UiContext& ui) {
    ImGui::Checkbox("Info", &showInfo);
    ImGui::SameLine();
    ImGui::Checkbox("Warnings", &showWarnings);
    ImGui::SameLine();
    ImGui::Checkbox("Errors", &showErrors);

    const ProblemsModel& model = Model(ui);
    ImGui::SameLine();
    ImGui::Text("%zu / %zu / %zu", model.info, model.warnings, model.errors);
    ImGui::Separator();

    if (model.rows.empty()) {
        Widgets::Hint("No problems");
        return;
    }

    auto* browser = dynamic_cast<DocumentBrowser*>(ui.panels.Find("onyx.browser"));
    const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
    Widgets::ForEachVisible(model.rows.size(), rowHeight, [&](size_t i) {
        const ProblemRow& row = model.rows[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::TextUnformatted(row.glyph);
        ImGui::SameLine();
        if (ImGui::Selectable(row.text.c_str(), false, ImGuiSelectableFlags_None) && browser && row.diag.at) {
            if (const Modules::Document* doc = ui.workspace.Get(row.doc)) {
                if (const auto path = FindEntry(*doc, *row.diag.at)) {
                    browser->Select(ui, Selection{row.doc, *path});
                }
            }
        }
        ImGui::PopID();
    });
}

}
