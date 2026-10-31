#include <doctest/doctest.h>

#include <Onyx/Core/Services/Settings.h>
#include <Onyx/Core/Vfs/OsFile.h>
#include <Onyx/Media/MediaView.h>
#include <Onyx/Ui/Command.h>
#include <Onyx/Ui/FrameClock.h>
#include <Onyx/Ui/PanelHost.h>
#include <Onyx/Ui/Theme.h>
#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/ViewerRegistry.h>

#include "../../Ui/Gpu/Support/UiGpuFixture.h"
#include "../Support/ClipFixture.h"

#include <chrono>
#include <memory>
#include <thread>

using namespace Onyx;
using namespace std::chrono_literals;

TEST_CASE("MediaView uploads the first frame into the pool") {
    const ClipFixture clip = WriteClip("gpu-view", false);
    auto opened = Vfs::OsFile::Open(clip.path);
    REQUIRE(opened.ok());

    auto fx = MakeUiGpuFixture();
    Gpu::RenderTargetDesc desc;
    desc.extent = {256, 256};
    desc.colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    desc.depthFormat = VK_FORMAT_UNDEFINED;
    desc.samples = VK_SAMPLE_COUNT_1_BIT;
    auto target = Gpu::RenderTarget::Create(*fx->device, desc);
    REQUIRE_MESSAGE(target.ok(), target.error().code << ": " << target.error().message);

    Modules::Workspace workspace;
    Services::Settings settings = Services::Settings::Empty();
    Ui::Theme theme{settings};
    Ui::CommandRegistry commands;
    Ui::ViewerRegistry viewers;
    Ui::PanelHost panels;
    Ui::FrameClock clock;
    Ui::UiContext ui{workspace, fx->device.get(), nullptr, fx->textures.get(), theme, commands, viewers, panels, clock,
                     1.0f / 60};
    {
        Media::MediaView view(1, {}, "clip", opened.value(), Domain::ByteRange{0, clip.offset, clip.size});
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (view.Texture() == ImTextureID_Invalid && !view.Fault() && std::chrono::steady_clock::now() < deadline) {
            auto frame = RenderImGuiFrame(*fx, *target.value(), [&] {
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ImVec2(256, 256));
                ImGui::Begin("media", nullptr, ImGuiWindowFlags_NoSavedSettings);
                view.Draw(ui);
                ImGui::End();
            });
            REQUIRE_MESSAGE(frame.ok(), frame.error().code << ": " << frame.error().message);
            std::this_thread::sleep_for(5ms);
        }
        REQUIRE_FALSE(view.Fault().has_value());
        REQUIRE(view.Texture() != ImTextureID_Invalid);
        CHECK(fx->textures->Count() == 1);
        CHECK(fx->textures->Size(view.Texture()) == glm::uvec2(64, 48));
        CHECK(view.Uploads() == 1);

        const ImTextureID first = view.Texture();
        view.Source().SetVolume(0.0f);
        view.Source().Play();
        const auto playing = std::chrono::steady_clock::now() + 5s;
        while (view.Uploads() < 2 && !view.Fault() && std::chrono::steady_clock::now() < playing) {
            auto frame = RenderImGuiFrame(*fx, *target.value(), [&] {
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ImVec2(256, 256));
                ImGui::Begin("media", nullptr, ImGuiWindowFlags_NoSavedSettings);
                view.Draw(ui);
                ImGui::End();
            });
            REQUIRE_MESSAGE(frame.ok(), frame.error().code << ": " << frame.error().message);
            std::this_thread::sleep_for(5ms);
        }
        REQUIRE_FALSE(view.Fault().has_value());
        CHECK(view.Uploads() >= 2);
        CHECK(view.Texture() == first);
        CHECK(fx->textures->Count() == 1);
        fx->device->GraphicsQueue().WaitIdle();
        CHECK(fx->device->ValidationMessageCount() == 0);

        auto drawn = RenderImGuiFrame(*fx, *target.value(), [&] {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(256, 256));
            ImGui::Begin("media", nullptr, ImGuiWindowFlags_NoSavedSettings);
            view.Draw(ui);
            ImGui::End();
        });
        REQUIRE(drawn.ok());
    }
    CHECK(fx->textures->Count() == 0);
    fx->device->GraphicsQueue().WaitIdle();
    CHECK(fx->device->ValidationMessageCount() == 0);
}
