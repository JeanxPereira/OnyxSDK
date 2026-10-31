#include <doctest/doctest.h>

#include <cstddef>

#include "Onyx/Core/Services/Settings.h"
#include "Onyx/Ui/Theme.h"
#include "Onyx/Ui/Widgets.h"
#include "HeadlessImGui.h"

#include <imgui_internal.h>

namespace W = Onyx::Ui::Widgets;

namespace {

int Commands() {
    int total = 0;
    ImDrawData* data = ImGui::GetDrawData();
    for (int i = 0; i < data->CmdListsCount; ++i) {
        total += data->CmdLists[i]->IdxBuffer.Size;
    }
    return total;
}

template <class F>
int DrawFrames(HeadlessImGui& rig, int frames, F&& body) {
    for (int f = 0; f < frames; ++f) {
        rig.NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(400, 300));
        ImGui::Begin("w", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        body();
        ImGui::End();
        rig.EndFrame();
    }
    return Commands();
}

}

TEST_CASE("Each widget draws without asserts and emits geometry of its own") {
    HeadlessImGui rig;
    const int baseline = DrawFrames(rig, 3, [] {});
    const auto emits = [&](const char* name, auto&& widget) {
        CAPTURE(name);
        CHECK(DrawFrames(rig, 3, widget) > baseline);
    };
    emits("IconButton", [] { W::IconButton("a", "A"); });
    emits("ToolbarButton", [] { W::ToolbarButton("tool", true); });
    emits("Hint", [] { W::Hint("dim"); });
    emits("Spinner", [] { W::Spinner("s", 8.0f, 2.0f); });
    emits("StatusText info", [] { W::StatusText(Onyx::Services::Severity::Info, "info"); });
    emits("StatusText warning", [] { W::StatusText(Onyx::Services::Severity::Warning, "warn"); });
    emits("StatusText error", [] { W::StatusText(Onyx::Services::Severity::Error, "err"); });
    emits("TreeRow", [] {
        if (W::TreeRow("t", "Row", "G", glm::vec4(1, 1, 1, 1), ImGuiTreeNodeFlags_None, true)) {
            ImGui::TreePop();
        }
    });
}

TEST_CASE("A disabled IconButton still shows its tooltip") {
    HeadlessImGui rig;
    W::IconButtonOptions options;
    options.disabled = true;
    options.tooltip = "Not now";
    auto body = [&] { W::IconButton("a", "A", options); };
    DrawFrames(rig, 2, body);
    ImGui::GetIO().AddMousePosEvent(16.0f, 16.0f);
    DrawFrames(rig, 60, body);

    bool tooltip = false;
    for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows) {
        tooltip = tooltip || ((window->Flags & ImGuiWindowFlags_Tooltip) != 0 && window->WasActive);
    }
    CHECK(tooltip);
}

TEST_CASE("IconButton reports a click") {
    HeadlessImGui rig;
    ImGuiIO& io = ImGui::GetIO();
    bool clicked = false;
    auto body = [&] {
        ImVec2 p = ImGui::GetCursorScreenPos();
        (void)p;
        clicked |= W::IconButton("a", "A");
    };
    DrawFrames(rig, 2, body);

    io.AddMousePosEvent(16.0f, 16.0f);
    DrawFrames(rig, 1, body);
    io.AddMouseButtonEvent(0, true);
    DrawFrames(rig, 2, body);
    io.AddMouseButtonEvent(0, false);
    DrawFrames(rig, 2, body);
    CHECK(clicked);
}

TEST_CASE("ForEachVisible visits only the visible rows") {
    HeadlessImGui rig;
    size_t calls = 0;
    DrawFrames(rig, 3, [&] {
        calls = 0;
        W::ForEachVisible(100000, 20.0f, [&](size_t) {
            ++calls;
            ImGui::Dummy(ImVec2(10, 20));
        });
    });
    CHECK(calls > 0);
    CHECK(calls < 100);
}

TEST_CASE("SplitterH keeps the minimums and the sum") {
    HeadlessImGui rig;
    ImGuiIO& io = ImGui::GetIO();
    float left = 100.0f;
    float right = 200.0f;
    auto body = [&] { W::SplitterH("sp", &left, &right, 80.0f, 150.0f); };
    DrawFrames(rig, 2, body);
    io.AddMousePosEvent(10.0f, 20.0f);
    DrawFrames(rig, 1, body);
    io.AddMouseButtonEvent(0, true);
    DrawFrames(rig, 2, body);
    io.AddMousePosEvent(900.0f, 20.0f);
    DrawFrames(rig, 2, body);
    CHECK(left + right == doctest::Approx(300.0f));
    CHECK(left >= 80.0f);
    CHECK(right >= 150.0f);
    CHECK(left == doctest::Approx(150.0f));
    io.AddMousePosEvent(-900.0f, 20.0f);
    DrawFrames(rig, 2, body);
    CHECK(left == doctest::Approx(80.0f));
    CHECK(right == doctest::Approx(220.0f));
    io.AddMouseButtonEvent(0, false);
    DrawFrames(rig, 1, body);
}

TEST_CASE("SplitterH grows with the UI scale") {
    HeadlessImGui rig;
    auto settings = Onyx::Services::Settings::Empty();
    Onyx::Ui::Theme theme(settings);
    theme.Commit();
    float left = 100.0f;
    float right = 200.0f;
    float width = 0.0f;
    auto body = [&] {
        W::SplitterH("sp", &left, &right, 80.0f, 150.0f);
        width = ImGui::GetItemRectSize().x;
    };
    DrawFrames(rig, 2, body);
    const float unscaled = width;

    theme.Mutate([](Onyx::Ui::ThemeState& state) { state.userScale = 2.0f; });
    theme.Commit();
    DrawFrames(rig, 2, body);
    CHECK(width == doctest::Approx(unscaled * 2.0f));
}

TEST_CASE("Metrics follow the applied style") {
    HeadlessImGui rig;
    auto settings = Onyx::Services::Settings::Empty();
    Onyx::Ui::Theme theme(settings);
    theme.Commit();
    const auto m = W::MetricsFor(theme);
    CHECK(m.smallButtonInset >= 2.0f);
    CHECK(m.iconButtonSize > 0.0f);
    CHECK(m.rowHeight >= m.iconButtonSize - 0.01f);
    const ImGuiStyle& style = theme.Applied().style;
    CHECK(m.iconButtonSize == doctest::Approx(style.FontSizeBase + style.FramePadding.y * 2.0f));

    theme.Mutate([](Onyx::Ui::ThemeState& state) { state.fontSizePt = 24.0f; });
    theme.Commit();
    CHECK(W::MetricsFor(theme).iconButtonSize > m.iconButtonSize);
}
