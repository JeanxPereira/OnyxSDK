#include <Onyx/App/InfoTab.h>
#include <Onyx/App/UIHelpers.h>
#include <Onyx/App/InfoTabFilter.h>
#include "imgui.h"
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>

// helper: read-only property row
static void PropRow(const char* key, const std::string& value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextDisabled("%s", key);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextWrapped("%s", value.c_str());
    // Copy on click
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(1)) {
        ImGui::SetClipboardText(value.c_str());
    }
}

static std::string FormatHex32(uint32_t v) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::setfill('0') << std::setw(8) << v;
    return ss.str();
}

static std::string FormatFloat(float v) {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(4) << v;
    return ss.str();
}

namespace Onyx::App {

// M3b Task 6: InfoTab is now hosted by InspectorPanel (Include/Onyx/App/
// Panels/InspectorPanel.h), registered alongside DocumentBrowser in
// App::registerPanels(). The profile-era Draw(AssetDatabase&, AssetEntry*)
// overload that used to coexist here (see prior revisions) is gone along
// with AssetDatabase itself -- it never had a caller in this tree.
InfoTab::InfoTab(Onyx::Modules::Workspace& workspace)
    : m_workspace(workspace),
      m_selectionSub(workspace.Events().On<Onyx::Modules::SelectionChanged>(
          [this](const Onyx::Modules::SelectionChanged& ev) {
              m_selDoc = ev.doc;
              m_selPath = ev.path;
              m_hasSelection = true;
          })) {}

void InfoTab::Draw() {
    if (!m_hasSelection) {
        ImGui::TextDisabled("No selection.");
        return;
    }

    Onyx::Modules::Document* doc = m_workspace.Get(m_selDoc);
    // Thread rule (Workspace.h): a document's roots/diags are unsafe to
    // read before its parse finishes -- `ready` is the only field safe to
    // poll on a document that might still be mid-parse on a worker
    // thread. A document that has since closed (Get() returns null) is
    // the same "nothing to show" case as one that is still loading.
    if (!doc || !doc->ready.load()) {
        ImGui::TextDisabled("selection stale");
        return;
    }

    const Onyx::Domain::AssetEntry* entry = Onyx::Modules::Resolve(*doc, m_selPath);
    if (!entry) {
        // The path no longer resolves -- the document reparsed to a
        // smaller tree, or the selected node moved/vanished.
        ImGui::TextDisabled("selection stale");
        return;
    }

    const bool failed = (static_cast<uint8_t>(entry->flags) & static_cast<uint8_t>(Onyx::Domain::NodeFlags::Failed)) != 0;
    const std::string& label = entry->displayName.empty() ? entry->name : entry->displayName;

    if (ImGui::BeginTable("##wsprops", 2,
        ImGuiTableFlags_SizingFixedFit |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_BordersInnerH))
    {
        ImGui::TableSetupColumn("Key",   ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        PropRow("Name",   label);
        PropRow("Type",   std::string(m_workspace.Catalog().KeyOf(entry->typeId)));
        PropRow("Size",   FormatBytes(entry->size));
        PropRow("Offset", FormatHex32(entry->offset));

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("Flags");
        ImGui::TableSetColumnIndex(1);
        if (failed) {
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.15f, 1.0f), "Failed");
        } else {
            ImGui::TextUnformatted("None");
        }

        ImGui::EndTable();
    }

    // Diags whose message names this entry -- see InfoTabFilter.h's file
    // comment: substring match on message text is the only diag<->node
    // association available until diags carry a NodePath of their own.
    std::vector<Onyx::Services::Diag> diags = doc->diags.Snapshot();
    bool anyDiag = false;
    for (const auto& d : diags) {
        if (!DiagMentionsEntry(d, label)) continue;
        if (!anyDiag) {
            ImGui::Spacing();
            ImGui::SeparatorText("Diagnostics");
            anyDiag = true;
        }
        const char* sevTag = d.severity == Onyx::Services::Severity::Error   ? "[error] "
                            : d.severity == Onyx::Services::Severity::Warning ? "[warn] "
                                                                               : "[info] ";
        ImGui::TextWrapped("%s%s", sevTag, d.message.c_str());
    }
}

} // namespace Onyx::App
