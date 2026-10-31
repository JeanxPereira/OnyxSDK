#include <doctest/doctest.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <chrono>

#include "Onyx/Ui/SfSymbols.h"
#include "Onyx/Ui/Widgets.h"
#include "Support/WindowFixture.h"

using namespace Onyx;

namespace {

ImVec2 g_buttonCentre{};

void DrawHoverWindow(Gpu::Frame&) {
    ImGui::SetNextWindowPos({16.0f, 16.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({220.0f, 120.0f}, ImGuiCond_Always);
    ImGui::Begin("Probe");
    Ui::Widgets::IconButtonOptions options;
    options.tooltip = "Hover me";
    Ui::Widgets::IconButton("##hover", ICON_SF_GEAR, options);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    g_buttonCentre = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    ImGui::End();
}

}

TEST_CASE("a pending hover delay keeps the frame polling") {
    auto fx = MakeWindowFixture();
    fx->shell->SetIdleWaitCap(0.5);
    for (int i = 0; i < 4; ++i) {
        fx->window->PollEvents();
        REQUIRE(fx->shell->Step(DrawHoverWindow).ok());
    }

    ImGui::GetIO().AddMousePosEvent(g_buttonCentre.x, g_buttonCentre.y);
    for (int i = 0; i < 5; ++i) {
        auto stepped = fx->shell->Step(DrawHoverWindow);
        REQUIRE_MESSAGE(stepped.ok(), stepped.error().code << ": " << stepped.error().message);
    }
    const ImGuiContext* context = ImGui::GetCurrentContext();
    REQUIRE(context->HoverItemDelayId != 0);
    REQUIRE(context->HoverItemDelayTimer < context->Style.HoverDelayNormal);

    const auto before = std::chrono::steady_clock::now();
    for (int i = 0; i < 3; ++i) {
        auto stepped = fx->shell->Step(DrawHoverWindow);
        REQUIRE_MESSAGE(stepped.ok(), stepped.error().code << ": " << stepped.error().message);
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - before).count();
    CHECK(elapsed < 0.25);
    fx->device->GraphicsQueue().WaitIdle();
    CHECK(fx->device->ValidationMessageCount() == 0);
}
