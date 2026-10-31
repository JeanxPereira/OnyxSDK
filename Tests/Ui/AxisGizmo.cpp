#include <doctest/doctest.h>

#include "../../Source/Ui/Panels/AxisGizmo.h"
#include "Support/HeadlessImGui.h"

using namespace Onyx;

namespace {

Ui::GizmoHit Frame(HeadlessImGui& imgui, ImVec2 mouse, bool down, const glm::mat3& rotation) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(mouse.x, mouse.y);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
    imgui.NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(200, 200));
    ImGui::Begin("gizmo", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
    const Ui::GizmoHit hit = Ui::DrawAxisGizmo(ImGui::GetWindowDrawList(), ImVec2(100, 100), 38.0f, rotation);
    ImGui::End();
    imgui.EndFrame();
    return hit;
}

Ui::GizmoHit Click(HeadlessImGui& imgui, ImVec2 at, const glm::mat3& rotation) {
    Frame(imgui, at, false, rotation);
    Frame(imgui, at, false, rotation);
    return Frame(imgui, at, true, rotation);
}

}

TEST_CASE("DrawAxisGizmo returns the +X view when the X cap is clicked") {
    HeadlessImGui imgui;
    const Ui::GizmoHit hit = Click(imgui, ImVec2(138, 100), glm::mat3(1.0f));
    REQUIRE(hit.view.has_value());
    CHECK(*hit.view == Render::CameraView::Right);
    CHECK(hit.hovered);
}

TEST_CASE("DrawAxisGizmo picks the cap nearest the viewer where caps overlap") {
    HeadlessImGui imgui;
    const Ui::GizmoHit hit = Click(imgui, ImVec2(100, 100), glm::mat3(1.0f));
    REQUIRE(hit.view.has_value());
    CHECK(*hit.view == Render::CameraView::Front);
}

TEST_CASE("DrawAxisGizmo follows the view rotation") {
    HeadlessImGui imgui;
    // A quarter turn about Y brings +Z to where +X was, so the cap on the right faces Front.
    const glm::mat3 rotation(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 1, 0)));
    const Ui::GizmoHit hit = Click(imgui, ImVec2(138, 100), rotation);
    REQUIRE(hit.view.has_value());
    CHECK(*hit.view == Render::CameraView::Front);
}

TEST_CASE("DrawAxisGizmo ignores clicks away from every cap and hover without a press") {
    HeadlessImGui imgui;
    const Ui::GizmoHit away = Click(imgui, ImVec2(20, 20), glm::mat3(1.0f));
    CHECK_FALSE(away.view.has_value());
    CHECK_FALSE(away.hovered);

    Frame(imgui, ImVec2(0, 0), false, glm::mat3(1.0f));
    const Ui::GizmoHit over = Frame(imgui, ImVec2(138, 100), false, glm::mat3(1.0f));
    CHECK(over.hovered);
    CHECK_FALSE(over.view.has_value());
}
