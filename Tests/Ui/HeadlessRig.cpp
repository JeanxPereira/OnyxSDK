#include <doctest/doctest.h>

#include "HeadlessImGui.h"

TEST_CASE("Headless ImGui renders a frame") {
    HeadlessImGui rig;
    // An auto-sized window draws nothing on its first frame; the later frames show a steady state.
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
