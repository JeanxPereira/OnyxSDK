#include <doctest/doctest.h>

#include "Support/HeadlessImGui.h"

TEST_CASE("Headless ImGui renders a frame") {
    HeadlessImGui rig;
    // A window is hidden for its first frame while it sizes itself, so draw data appears on the second.
    for (int frame = 0; frame < 3; ++frame) {
        rig.NewFrame();
        ImGui::Begin("x");
        ImGui::TextUnformatted("headless");
        ImGui::End();
        rig.EndFrame();
    }

    const ImDrawData* draw = ImGui::GetDrawData();
    REQUIRE(draw != nullptr);
    CHECK(draw->CmdListsCount >= 1);
    CHECK(draw->TotalVtxCount > 0);
}
