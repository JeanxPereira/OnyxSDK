#include <doctest/doctest.h>

#include <Onyx/TestKit/SceneFixtures.h>
#include <Onyx/Ui/Panels/Transport.h>
#include <Onyx/Ui/Viewers/DocumentTabs.h>
#include <Onyx/Ui/Viewers/SceneView.h>

#include "Support/BareContext.h"
#include "HeadlessImGui.h"

#include <memory>

using namespace Onyx;

TEST_CASE("Transport::Model is unavailable without a clip") {
    Ui::SceneSession session;
    const Ui::Transport::Model model = Ui::Transport::Read(session);
    CHECK_FALSE(model.available);
    CHECK(model.clips.empty());
}

TEST_CASE("Transport::Model reflects the animated chain's clips and the player state") {
    TestKit::SceneFixture chain = TestKit::BuildAnimatedChain();
    Ui::SceneSession session;
    session.BindAnimation(chain.scene);

    Ui::Transport::Model model = Ui::Transport::Read(session);
    REQUIRE(model.available);
    REQUIRE(model.clips.size() == 1);
    CHECK(model.clips[0] == "bend");
    CHECK(model.clip == 0);
    CHECK_FALSE(model.playing);
    CHECK(model.duration == doctest::Approx(1.0f).epsilon(1e-3));
    CHECK(model.time == 0.0f);
    CHECK(model.speed == 1.0f);
    CHECK(model.loop == Render::LoopMode::Loop);

    session.Play();
    session.SetSpeed(2.0f);
    session.SetLoop(Render::LoopMode::PingPong);
    session.Tick(0.25f);
    model = Ui::Transport::Read(session);
    CHECK(model.playing);
    CHECK(model.time == doctest::Approx(0.5f).epsilon(1e-3));
    CHECK(model.speed == 2.0f);
    CHECK(model.loop == Render::LoopMode::PingPong);

    session.Pause();
    CHECK_FALSE(Ui::Transport::Read(session).playing);
    session.Stop();
    CHECK(Ui::Transport::Read(session).time == 0.0f);
}

TEST_CASE("Transport draws against the active scene tab and its buttons drive the player") {
    HeadlessImGui imgui;
    BareContext rig;
    auto tabs = std::make_unique<Ui::DocumentTabs>(rig.workspace);
    Ui::DocumentTabs* tabsPtr = tabs.get();
    rig.panels.Add(std::move(tabs));

    Ui::UiContext ui = rig.Make();
    Ui::Transport transport;
    const auto frame = [&] {
        imgui.NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(600, 80));
        ImGui::Begin("transport", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);
        transport.Draw(ui);
        ImGui::End();
        imgui.EndFrame();
    };
    frame();
    frame();

    TestKit::SceneFixture chain = TestKit::BuildAnimatedChain();
    auto view = std::make_unique<Ui::SceneView>(1, Modules::NodePath{{0}}, "chain",
                                                std::make_unique<Domain::Scene>(std::move(chain.scene)));
    view->Session().BindAnimation(view->Source());
    Ui::SceneSession& session = view->Session();
    tabsPtr->Add(std::move(view));
    for (int i = 0; i < 3; ++i) frame();
    CHECK_FALSE(session.Player().IsPlaying());

    // The play button is the first widget of the window.
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 button(style.WindowPadding.x + 8.0f, style.WindowPadding.y + 8.0f);
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(button.x, button.y);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    frame();
    CHECK(session.Player().IsPlaying());
}
