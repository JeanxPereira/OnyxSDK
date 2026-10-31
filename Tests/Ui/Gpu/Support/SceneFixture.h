#pragma once

#include "../../Support/ObxWorkspace.h"

#include <Onyx/Gpu/FrameRing.h>
#include <Onyx/Gpu/RenderTarget.h>
#include <Onyx/TestKit/SceneFixtures.h>
#include <Onyx/Ui/Panels/Viewport.h>
#include <Onyx/Ui/Viewers/DocumentTabs.h>
#include <Onyx/Ui/Viewers/Opening.h>
#include <Onyx/Ui/Viewers/SceneView.h>

#include "UiGpuFixture.h"

#include <functional>
#include <memory>

namespace Onyx::TestSupport {

// One host frame the way a shell runs it: the frame is begun before the panels draw, so panels can
// record their own passes into it, and the UI pass is recorded last into `target`.
inline Result<TestKit::Image8> RenderHostFrame(UiGpuFixture& fx, Gpu::RenderTarget& target,
                                               const std::function<void(Gpu::Frame&)>& draw) {
    using Out = Result<TestKit::Image8>;
    const VkExtent2D extent = target.Extent();
    ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(extent.width), static_cast<float>(extent.height));

    auto frame = fx.frames->Begin();
    if (!frame.ok()) {
        return Out::Fail(frame.error());
    }
    fx.backend->NewFrame();
    fx.imgui.NewFrame();
    draw(*frame.value());
    ImGui::Render();

    frame.value()->AddWait(fx.textures->Flush());
    target.Begin(frame.value()->Cmd(), {0.0f, 0.0f, 0.0f, 1.0f});
    fx.backend->Record(frame.value()->Cmd(), ImGui::GetDrawData());
    target.End(frame.value()->Cmd());
    auto fault = fx.backend->Fault();
    if (!fault.ok()) {
        return Out::Fail(fault.error());
    }
    auto ended = fx.frames->End(*frame.value());
    if (!ended.ok()) {
        return Out::Fail(ended.error());
    }
    auto pixels = Gpu::ReadbackColor(*fx.device, target, ended.value());
    if (!pixels.ok()) {
        return Out::Fail(pixels.error());
    }
    TestKit::Image8 image;
    image.width = extent.width;
    image.height = extent.height;
    image.rgba = std::move(pixels.value());
    return Out::Ok(std::move(image));
}

// The same frame without the readback: it returns once the frame is submitted, so the next call
// begins while this one may still be executing.
inline Result<void> SubmitHostFrame(UiGpuFixture& fx, Gpu::RenderTarget& target,
                                    const std::function<void(Gpu::Frame&)>& draw) {
    const VkExtent2D extent = target.Extent();
    ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(extent.width), static_cast<float>(extent.height));

    auto frame = fx.frames->Begin();
    if (!frame.ok()) {
        return Result<void>::Fail(frame.error());
    }
    fx.backend->NewFrame();
    fx.imgui.NewFrame();
    draw(*frame.value());
    ImGui::Render();

    frame.value()->AddWait(fx.textures->Flush());
    target.Begin(frame.value()->Cmd(), {0.0f, 0.0f, 0.0f, 1.0f});
    fx.backend->Record(frame.value()->Cmd(), ImGui::GetDrawData());
    target.End(frame.value()->Cmd());
    auto fault = fx.backend->Fault();
    if (!fault.ok()) {
        return Result<void>::Fail(fault.error());
    }
    auto ended = fx.frames->End(*frame.value());
    if (!ended.ok()) {
        return Result<void>::Fail(ended.error());
    }
    return Result<void>::Ok();
}

inline std::unique_ptr<Gpu::RenderTarget> MakeHostTarget(Gpu::Device& device, uint32_t size = 256) {
    Gpu::RenderTargetDesc desc;
    desc.extent = {size, size};
    desc.colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    desc.depthFormat = VK_FORMAT_UNDEFINED;
    desc.samples = VK_SAMPLE_COUNT_1_BIT;
    auto target = Gpu::RenderTarget::Create(device, desc);
    REQUIRE_MESSAGE(target.ok(), target.error().code << ": " << target.error().message);
    return std::move(target.value());
}

// The obx workspace of ObxUi with a document-tabs panel and the scene viewer registered.
struct SceneRig {
    ObxUi obx;
    Ui::DocumentTabs* tabs = nullptr;

    SceneRig() {
        Ui::RegisterSceneViewer(obx.viewers);
        auto owned = std::make_unique<Ui::DocumentTabs>(obx.workspace);
        tabs = owned.get();
        obx.panels.Add(std::move(owned));
    }

    Ui::UiContext Context(UiGpuFixture& fx, Gpu::Frame* frame, float dt = 1.0f / 60) {
        Ui::UiContext ui = obx.Make();
        ui.gpu = fx.device.get();
        ui.frame = frame;
        ui.textures = fx.textures.get();
        ui.dt = dt;
        return ui;
    }

    // Opens the obx mesh through OpenEntry and waits for the view to be placed.
    Ui::SceneView* OpenMesh(UiGpuFixture& fx) {
        Ui::UiContext ui = Context(fx, nullptr);
        const auto opened = Ui::OpenEntry(ui, obx.Pick(ObxWorkspace::kMesh),
                                          [&](std::unique_ptr<Ui::IDocumentView> v) { tabs->Add(std::move(v)); });
        REQUIRE_MESSAGE(opened.ok(), opened.error().code << ": " << opened.error().message);
        REQUIRE(obx.PumpUntil([&] { return tabs->Count() == 1; }));
        auto* view = dynamic_cast<Ui::SceneView*>(tabs->Active());
        REQUIRE(view != nullptr);
        return view;
    }

    Ui::SceneView* OpenChain() {
        TestKit::SceneFixture chain = TestKit::BuildAnimatedChain();
        Ui::OpenRequest request{.workspace = obx.workspace,
                                .doc = obx.doc,
                                .kind = Domain::MediaKind::Mesh,
                                .name = "chain",
                                .scene = std::make_unique<Domain::Scene>(std::move(chain.scene))};
        tabs->Add(Ui::CreateSceneView(request));
        auto* view = dynamic_cast<Ui::SceneView*>(tabs->Active());
        REQUIRE(view != nullptr);
        return view;
    }
};

inline void BeginFlatWindow(const char* name, float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin(name, nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();
}

// Pixels inside [x0, x1) x [y0, y1) that differ from the first pixel of the rect.
inline size_t CountDistinct(const TestKit::Image8& image, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
    const auto at = [&](uint32_t x, uint32_t y) { return (static_cast<size_t>(y) * image.width + x) * 4; };
    const size_t ref = at(x0, y0);
    size_t count = 0;
    for (uint32_t y = y0; y < y1; ++y) {
        for (uint32_t x = x0; x < x1; ++x) {
            const size_t i = at(x, y);
            for (size_t c = 0; c < 3; ++c) {
                if (image.rgba[i + c] != image.rgba[ref + c]) {
                    ++count;
                    break;
                }
            }
        }
    }
    return count;
}

}
