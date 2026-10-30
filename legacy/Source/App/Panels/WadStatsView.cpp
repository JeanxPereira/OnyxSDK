#include <Onyx/App/Panels/WadStatsView.h>

#include "imgui.h"

namespace Onyx::Viewers {

// M3b Task 6: this panel's data source was AssetDatabase::wads, read
// through Onyx::Api::Database() -- both gone along with the profile-era
// loading layer. Left as an empty-state panel body (not deleted: the
// per-type distribution/size charts are generic value once a Workspace
// document exposes the equivalent stats) rather than wired to anything,
// since no GameModule surfaces per-document type/size aggregates yet.
void WadStatsView::computeStats() {
    m_stats.clear();
    m_lastWadCount = 0;
    m_dirty = false;
}

void WadStatsView::Draw() {
    if (!visible) return;

    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("WAD Stats", &visible)) {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("WAD Stats ports with the game modules.");
    ImGui::TextDisabled(
        "Per-document type/size aggregates aren't wired to the Workspace yet.");

    ImGui::End();
}

} // namespace Onyx::Viewers
