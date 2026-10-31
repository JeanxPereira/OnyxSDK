#include <doctest/doctest.h>

#include <GLFW/glfw3.h>

#include <imgui.h>

#include "Support/WindowFixture.h"

using namespace Onyx;

namespace {

void DrawDemoWindow(Gpu::Frame&) {
    ImGui::SetNextWindowPos({16.0f, 16.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({220.0f, 120.0f}, ImGuiCond_Always);
    ImGui::Begin("Probe");
    ImGui::TextUnformatted("Onyx");
    ImGui::Button("Button");
    ImGui::End();
}

Result<bool> StepRedrawing(WindowFixture& fx, const std::function<void(Gpu::Frame&)>& draw = DrawDemoWindow) {
    fx.clock.RequestRedraw();
    return fx.shell->Step(draw);
}

}

TEST_CASE("The presenter acquires an image, renders into it and presents it") {
    auto fx = MakeWindowFixture();
    CHECK(fx->presenter->Extent().width == 640);
    CHECK(fx->presenter->Extent().height == 360);
    CHECK(fx->presenter->ImageCount() >= 2);

    auto frame = fx->frames->Begin();
    REQUIRE(frame.ok());
    auto image = fx->presenter->Acquire(*frame.value());
    REQUIRE_MESSAGE(image.ok(), image.error().code << ": " << image.error().message);
    REQUIRE(image.value().has_value());
    fx->presenter->BeginRendering(frame.value()->Cmd(), *image.value(), {0.1f, 0.2f, 0.3f, 1.0f});
    fx->presenter->EndRendering(frame.value()->Cmd(), *image.value());
    auto ended = fx->frames->End(*frame.value());
    REQUIRE(ended.ok());
    auto presented = fx->presenter->Present(ended.value(), *image.value());
    CHECK_MESSAGE(presented.ok(), presented.error().code << ": " << presented.error().message);
    fx->device->GraphicsQueue().WaitIdle();
    CHECK(fx->device->ValidationMessageCount() == 0);
}

TEST_CASE("ShellFrame steps five frames drawing a window") {
    auto fx = MakeWindowFixture();
    for (int i = 0; i < 5; ++i) {
        auto stepped = StepRedrawing(*fx);
        REQUIRE_MESSAGE(stepped.ok(), stepped.error().code << ": " << stepped.error().message);
        CHECK(stepped.value());
    }
    fx->device->GraphicsQueue().WaitIdle();
    CHECK(fx->device->ValidationMessageCount() == 0);
}

TEST_CASE("The draw callback runs once per step with an open frame") {
    auto fx = MakeWindowFixture();
    int calls = 0;
    auto stepped = StepRedrawing(*fx, [&](Gpu::Frame& frame) {
        ++calls;
        CHECK(frame.Cmd() != VK_NULL_HANDLE);
        DrawDemoWindow(frame);
    });
    REQUIRE(stepped.ok());
    CHECK(calls == 1);
}

TEST_CASE("ShellFrame recreates the swapchain after a resize") {
    auto fx = MakeWindowFixture();
    REQUIRE(StepRedrawing(*fx).ok());

    glfwSetWindowSize(fx->window->Handle(), 500, 300);
    fx->window->PollEvents();
    const glm::ivec2 size = fx->window->FramebufferSize();
    REQUIRE(size.x > 0);

    auto stepped = StepRedrawing(*fx);
    REQUIRE_MESSAGE(stepped.ok(), stepped.error().code << ": " << stepped.error().message);
    CHECK(stepped.value());
    CHECK(fx->presenter->Extent().width == static_cast<uint32_t>(size.x));
    CHECK(fx->presenter->Extent().height == static_cast<uint32_t>(size.y));

    // A surface with a fixed current extent wins over the requested size, so the window size is the answer.
    auto resized = fx->presenter->Resize({420, 240});
    REQUIRE_MESSAGE(resized.ok(), resized.error().code << ": " << resized.error().message);
    CHECK(fx->presenter->Extent().width == static_cast<uint32_t>(size.x));
    REQUIRE(StepRedrawing(*fx).ok());
    fx->device->GraphicsQueue().WaitIdle();
    CHECK(fx->device->ValidationMessageCount() == 0);
}

TEST_CASE("SetShouldClose makes Step return false") {
    auto fx = MakeWindowFixture();
    REQUIRE(StepRedrawing(*fx).ok());
    fx->window->SetShouldClose(true);
    auto stepped = StepRedrawing(*fx);
    REQUIRE(stepped.ok());
    CHECK_FALSE(stepped.value());
}

TEST_CASE("Step called from the draw callback is refused") {
    auto fx = MakeWindowFixture();
    Result<bool> inner = Result<bool>::Ok(true);
    auto outer = StepRedrawing(*fx, [&](Gpu::Frame& frame) {
        DrawDemoWindow(frame);
        inner = fx->shell->Step([](Gpu::Frame&) {});
    });
    REQUIRE(outer.ok());
    REQUIRE_FALSE(inner.ok());
    CHECK(inner.error().code == "shell.frame");
}

TEST_CASE("An idle clock waits for events up to its remaining animation") {
    auto fx = MakeWindowFixture();
    REQUIRE(StepRedrawing(*fx).ok());
    fx->clock.RequestAnimation(0.05);
    const double before = glfwGetTime();
    auto stepped = fx->shell->Step(DrawDemoWindow);
    REQUIRE(stepped.ok());
    CHECK(glfwGetTime() - before < 0.9);
    fx->device->GraphicsQueue().WaitIdle();
    CHECK(fx->device->ValidationMessageCount() == 0);
}
