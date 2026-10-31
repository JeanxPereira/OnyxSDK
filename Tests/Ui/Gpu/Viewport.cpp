#include <doctest/doctest.h>

#include "Support/SceneFixture.h"

using namespace Onyx;
using namespace Onyx::TestSupport;

namespace {

Result<TestKit::Image8> DrawViewport(UiGpuFixture& fx, SceneRig& rig, Gpu::RenderTarget& host, Ui::Viewport& viewport,
                                     float width, float height) {
    return RenderHostFrame(fx, host, [&](Gpu::Frame& frame) {
        Ui::UiContext ui = rig.Context(fx, &frame, 1.0f / 60);
        BeginFlatWindow("viewport", 0, 0, width, height);
        viewport.Draw(ui);
        ImGui::End();
    });
}

}

TEST_CASE("Viewport shows the mesh opened through OpenEntry") {
    auto fx = MakeUiGpuFixture();
    SceneRig rig;
    auto host = MakeHostTarget(*fx->device);
    Ui::SceneView* mesh = rig.OpenMesh(*fx);
    mesh->Session().Overlays().grid = false;

    {
        Ui::Viewport viewport;
        TestKit::Image8 image;
        for (int i = 0; i < 3; ++i) {
            auto drawn = DrawViewport(*fx, rig, *host, viewport, 256, 256);
            REQUIRE_MESSAGE(drawn.ok(), drawn.error().code << ": " << drawn.error().message);
            image = std::move(drawn.value());
        }
        CHECK_FALSE(viewport.Fault().has_value());
        CHECK(fx->textures->Count() == 1);
        // The region stays clear of the axis gizmo in the bottom-right corner and of the grid, which is off.
        CHECK(CountDistinct(image, 16, 16, 140, 140) > 500);
    }
    CHECK(fx->textures->Count() == 0);
    CHECK(fx->device->ValidationMessageCount() == 0);
}

TEST_CASE("Viewport re-registers its texture when the panel is resized") {
    auto fx = MakeUiGpuFixture();
    SceneRig rig;
    auto host = MakeHostTarget(*fx->device);
    rig.OpenMesh(*fx);

    Ui::Viewport viewport;
    for (int i = 0; i < 2; ++i) REQUIRE(DrawViewport(*fx, rig, *host, viewport, 256, 256).ok());
    const uint64_t before = viewport.TargetGeneration();
    CHECK(fx->textures->Count() == 1);

    for (int i = 0; i < 2; ++i) {
        auto drawn = DrawViewport(*fx, rig, *host, viewport, 192, 160);
        REQUIRE_MESSAGE(drawn.ok(), drawn.error().code << ": " << drawn.error().message);
    }
    CHECK(viewport.TargetGeneration() > before);
    CHECK(fx->textures->Count() == 1);
    CHECK_FALSE(viewport.Fault().has_value());
    CHECK(fx->device->ValidationMessageCount() == 0);
}

TEST_CASE("Viewport draws a placeholder without a frame and records no texture") {
    auto fx = MakeUiGpuFixture();
    SceneRig rig;
    auto host = MakeHostTarget(*fx->device);
    rig.OpenMesh(*fx);

    Ui::Viewport viewport;
    auto drawn = RenderHostFrame(*fx, *host, [&](Gpu::Frame&) {
        Ui::UiContext ui = rig.Context(*fx, nullptr);
        BeginFlatWindow("viewport", 0, 0, 256, 256);
        viewport.Draw(ui);
        ImGui::End();
    });
    REQUIRE(drawn.ok());
    CHECK(fx->textures->Count() == 0);
    CHECK(fx->device->ValidationMessageCount() == 0);
}

TEST_CASE("Closing the active tab in the frame the viewport drew it leaves no resource in use") {
    auto fx = MakeUiGpuFixture();
    SceneRig rig;
    auto host = MakeHostTarget(*fx->device);
    rig.OpenMesh(*fx);

    Ui::Viewport viewport;
    const auto frame = [&](bool close) {
        return SubmitHostFrame(*fx, *host, [&](Gpu::Frame& f) {
            Ui::UiContext ui = rig.Context(*fx, &f);
            BeginFlatWindow("viewport", 0, 0, 256, 128);
            viewport.Draw(ui);
            ImGui::End();
            if (close) rig.tabs->CloseActive();
            BeginFlatWindow("documents", 0, 128, 256, 128);
            rig.tabs->Draw(ui);
            ImGui::End();
        });
    };
    for (int i = 0; i < 3; ++i) REQUIRE(frame(false).ok());
    REQUIRE(frame(true).ok());
    CHECK(rig.tabs->Count() == 0);
    for (int i = 0; i < 4; ++i) {
        const auto stepped = frame(false);
        REQUIRE_MESSAGE(stepped.ok(), stepped.error().code << ": " << stepped.error().message);
    }
    REQUIRE(fx->device->GraphicsQueue().WaitIdle().ok());
    CHECK(fx->device->ValidationMessageCount() == 0);
}

TEST_CASE("A click on the +X axis cap snaps the camera to the right view") {
    auto fx = MakeUiGpuFixture();
    SceneRig rig;
    auto host = MakeHostTarget(*fx->device);
    Ui::SceneView* view = rig.OpenMesh(*fx);
    Ui::Viewport viewport;
    const auto frame = [&](float dt) {
        return RenderHostFrame(*fx, *host, [&](Gpu::Frame& f) {
            Ui::UiContext ui = rig.Context(*fx, &f, dt);
            BeginFlatWindow("viewport", 0, 0, 256, 256);
            viewport.Draw(ui);
            ImGui::End();
        });
    };
    for (int i = 0; i < 3; ++i) REQUIRE(frame(1.0f / 60).ok());

    Render::Camera& camera = view->Session().Camera();
    REQUIRE_FALSE(camera.IsAnimating());
    // The gizmo sits 16 px in from the bottom-right corner; the +X cap is the world X axis through the view.
    const glm::vec3 x = camera.ViewRotation() * glm::vec3(1.0f, 0.0f, 0.0f);
    const ImVec2 cap(256.0f - 16.0f - 38.0f + x.x * 38.0f, 256.0f - 16.0f - 38.0f - x.y * 38.0f);

    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(cap.x, cap.y);
    REQUIRE(frame(1.0f / 60).ok());
    REQUIRE(frame(1.0f / 60).ok());
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    REQUIRE(frame(1.0f / 60).ok());
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    CHECK(camera.IsAnimating());
    for (int i = 0; i < 30 && camera.IsAnimating(); ++i) REQUIRE(frame(0.1f).ok());
    CHECK_FALSE(camera.IsAnimating());

    const glm::vec3 offset = glm::normalize(camera.GetPosition() - camera.GetTarget());
    CHECK(offset.x == doctest::Approx(1.0f).epsilon(1e-2));
    CHECK(offset.y == doctest::Approx(0.0f).epsilon(1e-2));
    CHECK(offset.z == doctest::Approx(0.0f).epsilon(1e-2));
    CHECK(fx->device->ValidationMessageCount() == 0);
}
