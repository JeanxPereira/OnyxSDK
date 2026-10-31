#pragma once

#include <doctest/doctest.h>

#include "ValidationLayer.h"

#include <Onyx/Gpu/Device.h>
#include <Onyx/Gpu/FrameRing.h>
#include <Onyx/Gpu/RenderTarget.h>
#include <Onyx/Gpu/Upload.h>
#include <Onyx/TestKit/Png.h>
#include <Onyx/Ui/ImGuiVulkanBackend.h>
#include <Onyx/Ui/TexturePool.h>

#include <cstdlib>
#include <functional>
#include <memory>
#include <string_view>

#include "HeadlessImGui.h"

namespace Onyx {

struct UiGpuFixture {
    std::unique_ptr<Gpu::Device> device;
    std::unique_ptr<Gpu::UploadContext> upload;
    std::unique_ptr<Gpu::FrameRing> frames;
    std::unique_ptr<Ui::ImGuiVulkanBackend> backend;
    HeadlessImGui imgui;
    std::unique_ptr<Ui::TexturePool> textures;

    UiGpuFixture() = default;
    UiGpuFixture(const UiGpuFixture&) = delete;
    UiGpuFixture& operator=(const UiGpuFixture&) = delete;

    // The pool and the backend need the ImGui context and the device alive, so they go first.
    ~UiGpuFixture() {
        textures.reset();
        backend.reset();
    }
};

inline void MakeUiGpuFixture(UiGpuFixture& fx, VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM) {
    Gpu::DeviceOptions deviceOptions;
    deviceOptions.validation = true;
    auto device = Gpu::Device::Create(deviceOptions);
    REQUIRE_MESSAGE(device.ok(), device.error().code << ": " << device.error().message);
    fx.device = std::move(device.value());
    RequireValidationLayer(*fx.device);

    auto upload = Gpu::UploadContext::Create(*fx.device);
    REQUIRE_MESSAGE(upload.ok(), upload.error().code << ": " << upload.error().message);
    fx.upload = std::move(upload.value());

    auto frames = Gpu::FrameRing::Create(*fx.device);
    REQUIRE_MESSAGE(frames.ok(), frames.error().code << ": " << frames.error().message);
    fx.frames = std::move(frames.value());

    Ui::BackendOptions options;
    options.colorFormat = colorFormat;
    auto backend = Ui::ImGuiVulkanBackend::Create(*fx.device, options);
    REQUIRE_MESSAGE(backend.ok(), backend.error().code << ": " << backend.error().message);
    fx.backend = std::move(backend.value());

    fx.textures = std::make_unique<Ui::TexturePool>(*fx.device, *fx.upload, *fx.backend);
}

// Builds the fixture in place: it owns an ImGui context and so cannot be returned by value.
inline std::unique_ptr<UiGpuFixture> MakeUiGpuFixture(VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM) {
    auto fx = std::make_unique<UiGpuFixture>();
    MakeUiGpuFixture(*fx, colorFormat);
    return fx;
}

// One frame: NewFrame, `draw`, Render, a submitted command buffer that clears the target and records
// the draw data, then a readback (top row first).
inline Result<TestKit::Image8> RenderImGuiFrame(UiGpuFixture& fx, Gpu::RenderTarget& target,
                                                const std::function<void()>& draw,
                                                const glm::vec4& clear = {0.0f, 0.0f, 0.0f, 1.0f}) {
    using Out = Result<TestKit::Image8>;
    const VkExtent2D extent = target.Extent();
    ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(extent.width), static_cast<float>(extent.height));

    fx.backend->NewFrame();
    fx.imgui.NewFrame();
    draw();
    ImGui::Render();

    const Gpu::FenceValue uploaded = fx.textures->Flush();
    auto frame = fx.frames->Begin();
    if (!frame.ok()) {
        return Out::Fail(frame.error());
    }
    frame.value()->AddWait(uploaded);
    target.Begin(frame.value()->Cmd(), clear);
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

}
