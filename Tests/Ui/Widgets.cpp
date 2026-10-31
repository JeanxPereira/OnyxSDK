#include <doctest/doctest.h>

#include <cstddef>

#include "Onyx/Core/Services/Settings.h"
#include "Onyx/Ui/Theme.h"
#include "Onyx/Ui/Widgets.h"
#include "HeadlessImGui.h"

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

TEST_CASE("Widgets draw without asserts and emit geometry") {
    HeadlessImGui rig;
    const int idx = DrawFrames(rig, 3, [] {
        W::IconButton("a", "A");
        W::ToolbarButton("tool", true);
        W::Hint("dim");
        W::Spinner("s", 8.0f, 2.0f);
        W::StatusText(Onyx::Services::Severity::Info, "info");
        W::StatusText(Onyx::Services::Severity::Warning, "warn");
        W::StatusText(Onyx::Services::Severity::Error, "err");
        if (W::TreeRow("t", "Row", "G", glm::vec4(1, 1, 1, 1), ImGuiTreeNodeFlags_None, true)) {
            ImGui::TreePop();
        }
    });
    CHECK(idx > 0);
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

TEST_CASE("Metrics follow the applied style") {
    HeadlessImGui rig;
    auto settings = Onyx::Services::Settings::Empty();
    Onyx::Ui::Theme theme(settings);
    theme.Commit();
    const auto m = W::Metrics(theme);
    CHECK(m.smallButtonInset >= 2.0f);
    CHECK(m.iconButtonSize > 0.0f);
    CHECK(m.rowHeight >= m.iconButtonSize - 0.01f);
}
