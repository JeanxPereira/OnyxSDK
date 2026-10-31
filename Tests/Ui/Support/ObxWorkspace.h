#pragma once

#include <OnyxBox/Module.h>
#include <OnyxBox/Writer.h>

#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/Core/Services/Settings.h>
#include <Onyx/Ui/Command.h>
#include <Onyx/Ui/FrameClock.h>
#include <Onyx/Ui/PanelHost.h>
#include <Onyx/Ui/PathText.h>
#include <Onyx/Ui/Theme.h>
#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/ViewerRegistry.h>

#include <doctest/doctest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "Support/HeadlessImGui.h"

// A workspace with the obx module registered and one document opened from a freshly written
// .obx: image "img", text "txt", mesh "mesh", table "table", blob "blob", then `flatChildren`
// blobs named "flat<N>". `fileName` is UTF-8. The file is removed with the object.
struct ObxWorkspace {
    Onyx::Modules::Workspace workspace;
    Onyx::Modules::DocumentId doc = 0;
    std::filesystem::path file;

    ObxWorkspace(const std::filesystem::path& dir, size_t flatChildren, std::string_view fileName = "fixture.obx") {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        file = dir / Onyx::Ui::PathFromUtf8(fileName);

        OnyxBox::Writer writer;
        writer.AddImage("img", 8, 8, std::vector<std::byte>(8 * 8 * 4, std::byte{0x40}));
        writer.AddText("txt", "hello box");
        writer.AddMesh("mesh", {1.0f, 0.5f, 0.25f, 1.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f});
        writer.AddTable("table", {{"a", "b"}, {"c", "d"}});
        writer.AddBlob("blob", std::vector<std::byte>(16, std::byte{1}));
        for (size_t i = 0; i < flatChildren; ++i) {
            writer.AddBlob("flat" + std::to_string(i), std::vector<std::byte>(4, std::byte{2}));
        }
        const std::vector<std::byte> bytes = writer.Build();
        {
            std::ofstream out(file, std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }

        REQUIRE(workspace.AddModule(std::make_unique<OnyxBox::Module>()).ok());
        doc = workspace.Open(file, "obx");
        REQUIRE(doc != 0);
        workspace.Pump();
        REQUIRE(workspace.Get(doc)->ready.load());
    }

    ~ObxWorkspace() {
        std::error_code ec;
        std::filesystem::remove(file, ec);
    }

    ObxWorkspace(const ObxWorkspace&) = delete;
    ObxWorkspace& operator=(const ObxWorkspace&) = delete;
};

inline ObxWorkspace MakeObxWorkspace(const std::filesystem::path& dir, size_t flatChildren = 8) {
    return ObxWorkspace(dir, flatChildren);
}

// A UiContext over an ObxWorkspace: no GPU, a theme over empty settings and real registries.
struct ObxUi {
    ObxWorkspace obx;
    Onyx::Services::Settings settings = Onyx::Services::Settings::Empty();
    Onyx::Ui::Theme theme{settings};
    Onyx::Ui::CommandRegistry commands;
    Onyx::Ui::ViewerRegistry viewers;
    Onyx::Ui::PanelHost panels;
    Onyx::Ui::FrameClock clock;

    explicit ObxUi(const char* name, size_t flatChildren = 8, std::string_view fileName = "fixture.obx")
        : obx(std::filesystem::temp_directory_path() / (std::string("onyx_ui_") + name), flatChildren, fileName) {}

    ~ObxUi() {
        std::error_code ec;
        std::filesystem::remove_all(obx.file.parent_path(), ec);
    }

    Onyx::Ui::UiContext Make() {
        return Onyx::Ui::UiContext{obx.workspace, nullptr, nullptr, nullptr, theme,
                                   commands,      viewers, panels,  clock,   0.0f};
    }
};

// Draws `body` inside a fixed-size window for `frames` frames; a window that sizes itself draws
// nothing on its first frame.
template <class F>
void DrawFrames(HeadlessImGui& rig, int frames, F&& body) {
    for (int f = 0; f < frames; ++f) {
        rig.NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(500, 400));
        ImGui::Begin("panel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
        body();
        ImGui::End();
        rig.EndFrame();
    }
}
