#include <doctest/doctest.h>

#include "Support/SceneFixture.h"

#include <Onyx/Ui/Panels/Timeline.h>
#include <Onyx/Ui/Panels/Transport.h>

using namespace Onyx;
using namespace Onyx::TestSupport;

namespace {

Result<TestKit::Image8> DrawAll(UiGpuFixture& fx, SceneRig& rig, Gpu::RenderTarget& host, Ui::Viewport& viewport,
                                Ui::SceneView& view, float dt = 1.0f / 60) {
    return RenderHostFrame(fx, host, [&](Gpu::Frame& frame) {
        Ui::UiContext ui = rig.Context(fx, &frame, dt);
        BeginFlatWindow("viewport", 0, 0, 256, 160);
        viewport.Draw(ui);
        ImGui::End();
        BeginFlatWindow("scene", 0, 160, 256, 96);
        view.Draw(ui);
        ImGui::End();
    });
}

}

TEST_CASE("SceneView lists its parts once the viewport has loaded the scene") {
    auto fx = MakeUiGpuFixture();
    SceneRig rig;
    auto host = MakeHostTarget(*fx->device);
    Ui::SceneView* view = rig.OpenChain();
    CHECK_FALSE(view->Session().Loaded());
    CHECK(view->Id() == "onyx.scene." + std::to_string(rig.obx.doc) + ".chain");
    CHECK(view->Title() == "chain");

    Ui::Viewport viewport;
    for (int i = 0; i < 3; ++i) {
        auto drawn = DrawAll(*fx, rig, *host, viewport, *view);
        REQUIRE_MESSAGE(drawn.ok(), drawn.error().code << ": " << drawn.error().message);
    }
    REQUIRE(view->Session().Loaded());
    CHECK_FALSE(view->Fault().has_value());
    CHECK(view->Session().Scene().Draws().size() == view->Source().parts.size());
    CHECK(view->Session().HasSkeleton());
    CHECK(view->Session().HasAnimation());
    CHECK(fx->device->ValidationMessageCount() == 0);
}

TEST_CASE("Seeking the session changes the skinned image and a playing clip advances with the frame time") {
    auto fx = MakeUiGpuFixture();
    SceneRig rig;
    auto host = MakeHostTarget(*fx->device);
    Ui::SceneView* view = rig.OpenChain();
    view->Session().Overlays().skeleton = true;

    Ui::Viewport viewport;
    TestKit::Image8 rest;
    for (int i = 0; i < 3; ++i) {
        auto drawn = DrawAll(*fx, rig, *host, viewport, *view);
        REQUIRE(drawn.ok());
        rest = std::move(drawn.value());
    }
    Ui::SceneSession& session = view->Session();
    session.Seek(0.5f);
    TestKit::Image8 bent;
    for (int i = 0; i < 2; ++i) {
        auto drawn = DrawAll(*fx, rig, *host, viewport, *view);
        REQUIRE(drawn.ok());
        bent = std::move(drawn.value());
    }
    CHECK(rest.rgba != bent.rgba);

    session.Seek(0.0f);
    session.Play();
    for (int i = 0; i < 4; ++i) REQUIRE(DrawAll(*fx, rig, *host, viewport, *view, 0.1f).ok());
    CHECK(session.Player().Time() == doctest::Approx(0.4f).epsilon(0.01));
    CHECK(fx->device->ValidationMessageCount() == 0);
}

TEST_CASE("Transport and Timeline act on the scene tab the viewport shows") {
    auto fx = MakeUiGpuFixture();
    SceneRig rig;
    auto host = MakeHostTarget(*fx->device);
    Ui::SceneView* view = rig.OpenChain();
    Ui::Viewport viewport;
    Ui::Transport transport;
    Ui::Timeline timeline;

    for (int i = 0; i < 3; ++i) {
        auto drawn = RenderHostFrame(*fx, *host, [&](Gpu::Frame& frame) {
            Ui::UiContext ui = rig.Context(*fx, &frame);
            BeginFlatWindow("viewport", 0, 0, 256, 160);
            viewport.Draw(ui);
            ImGui::End();
            BeginFlatWindow("transport", 0, 160, 256, 48);
            transport.Draw(ui);
            ImGui::End();
            BeginFlatWindow("timeline", 0, 208, 256, 48);
            timeline.Draw(ui);
            ImGui::End();
        });
        REQUIRE_MESSAGE(drawn.ok(), drawn.error().code << ": " << drawn.error().message);
    }
    const Ui::Transport::Model model = Ui::Transport::Read(view->Session());
    CHECK(model.available);
    CHECK(model.clips.size() == 1);
    CHECK_FALSE(model.playing);
    CHECK(fx->device->ValidationMessageCount() == 0);
}
