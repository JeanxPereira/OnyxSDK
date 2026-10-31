#include <doctest/doctest.h>

#include <Onyx/TestKit/SceneFixtures.h>
#include <Onyx/Ui/Panels/Timeline.h>
#include <Onyx/Ui/Viewers/DocumentTabs.h>
#include <Onyx/Ui/Viewers/SceneView.h>

#include "Support/BareContext.h"
#include "Support/HeadlessImGui.h"

#include <memory>

using namespace Onyx;

TEST_CASE("Timeline maps pixels to time and back") {
    CHECK(Ui::Timeline::TimeAt(100.0f, 100.0f, 500.0f, 2.0f) == doctest::Approx(0.0f).epsilon(1e-3));
    CHECK(Ui::Timeline::TimeAt(300.0f, 100.0f, 500.0f, 2.0f) == doctest::Approx(1.0f).epsilon(1e-3));
    CHECK(Ui::Timeline::TimeAt(500.0f, 100.0f, 500.0f, 2.0f) == doctest::Approx(2.0f).epsilon(1e-3));
    CHECK(Ui::Timeline::TimeAt(0.0f, 100.0f, 500.0f, 2.0f) == 0.0f);
    CHECK(Ui::Timeline::TimeAt(900.0f, 100.0f, 500.0f, 2.0f) == 2.0f);
    CHECK(Ui::Timeline::TimeAt(250.0f, 100.0f, 100.0f, 2.0f) == 0.0f);
    CHECK(Ui::Timeline::PixelAt(1.5f, 100.0f, 500.0f, 2.0f) == doctest::Approx(400.0f).epsilon(1e-3));
    for (const float x : {100.0f, 137.0f, 262.5f, 499.0f}) {
        const float t = Ui::Timeline::TimeAt(x, 100.0f, 500.0f, 2.0f);
        CHECK(Ui::Timeline::PixelAt(t, 100.0f, 500.0f, 2.0f) == doctest::Approx(x).epsilon(1e-3));
    }
}

TEST_CASE("Timeline drag seeks the active clip to the time under the mouse") {
    HeadlessImGui imgui;
    BareContext rig;
    auto tabs = std::make_unique<Ui::DocumentTabs>(rig.workspace);
    Ui::DocumentTabs* tabsPtr = tabs.get();
    rig.panels.Add(std::move(tabs));
    TestKit::SceneFixture chain = TestKit::BuildAnimatedChain();
    auto scene = std::make_unique<Domain::Scene>(std::move(chain.scene));
    auto view = std::make_unique<Ui::SceneView>(1, Modules::NodePath{{0}}, "chain", std::move(scene));
    Ui::SceneSession& session = view->Session();
    session.BindAnimation(view->Source());
    session.Play();
    tabsPtr->Add(std::move(view));
    REQUIRE(session.Player().HasClip());
    const float duration = session.Player().Duration();
    REQUIRE(duration > 0.0f);

    Ui::UiContext ui = rig.Make();
    Ui::Timeline timeline;
    const float padding = ImGui::GetStyle().WindowPadding.x;
    const float left = padding;
    const float right = 400.0f - padding;

    const auto frame = [&](float x, bool down) {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(x, 20.0f);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
        imgui.NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(400, 100));
        ImGui::Begin("timeline", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        timeline.Draw(ui);
        ImGui::End();
        imgui.EndFrame();
    };

    frame(left + 10.0f, false);
    frame(left + 10.0f, false);
    frame(left + 0.5f * (right - left), true);
    CHECK_FALSE(session.Player().IsPlaying());
    CHECK(session.Player().Time() == doctest::Approx(0.5f * duration).epsilon(1e-3));

    frame(left + 0.25f * (right - left), true);
    CHECK(session.Player().Time() == doctest::Approx(0.25f * duration).epsilon(1e-3));

    frame(right + 80.0f, true);
    CHECK(session.Player().Time() == doctest::Approx(duration).epsilon(1e-3));
    frame(right + 80.0f, false);
}
