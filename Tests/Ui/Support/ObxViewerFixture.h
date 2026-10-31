#pragma once

#include <doctest/doctest.h>

#include <OnyxBox/Module.h>
#include <OnyxBox/Writer.h>

#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/Core/Services/Settings.h>
#include <Onyx/Ui/Command.h>
#include <Onyx/Ui/FrameClock.h>
#include <Onyx/Ui/PanelHost.h>
#include <Onyx/Ui/Selection.h>
#include <Onyx/Ui/Theme.h>
#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/ViewerRegistry.h>
#include <Onyx/Ui/Viewers/StockViewers.h>

#include "HeadlessImGui.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace Onyx::TestSupport {

// A workspace with the obx module and one document holding, in order: an 8x8 image, a three-line
// text, a 40-byte blob, a 3x2 table and a mesh. The stock viewers are registered.
struct ObxViewerFixture {
    static constexpr size_t kImage = 0;
    static constexpr size_t kText = 1;
    static constexpr size_t kBlob = 2;
    static constexpr size_t kTable = 3;
    static constexpr size_t kMesh = 4;
    static constexpr size_t kBlobSize = 40;

    static std::byte BlobByte(size_t i) { return static_cast<std::byte>((i * 3 + 1) & 0xFF); }

    ObxViewerFixture() {
        static int counter = 0;
        path = std::filesystem::temp_directory_path() / ("onyx_ui_viewers_" + std::to_string(++counter) + "_" + std::to_string(reinterpret_cast<uintptr_t>(this)) + ".obx");

        std::vector<std::byte> pixels(8 * 8 * 4);
        for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<std::byte>(i % 4 == 3 ? 255 : (i * 5) & 0xFF);
        std::vector<std::byte> blob(kBlobSize);
        for (size_t i = 0; i < blob.size(); ++i) blob[i] = BlobByte(i);

        OnyxBox::Writer writer;
        writer.AddImage("img", 8, 8, pixels);
        writer.AddText("txt", "alpha\nbeta\ngamma");
        writer.AddBlob("blob", blob);
        writer.AddTable("tbl", {{"3", "three"}, {"1", "one"}, {"2", "two"}});
        writer.AddMesh("mesh", {1, 1, 1, 1}, {0, 0, 0}, {1, 1, 1});
        const std::vector<std::byte> bytes = writer.Build();
        {
            std::ofstream f(path, std::ios::binary);
            f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }

        REQUIRE(workspace.AddModule(std::make_unique<OnyxBox::Module>()).ok());
        doc = workspace.Open(path, "obx");
        REQUIRE(doc != 0);
        for (int i = 0; i < 200 && !workspace.Get(doc)->ready; ++i) workspace.Pump();
        REQUIRE(workspace.Get(doc)->ready);
        REQUIRE(workspace.Get(doc)->roots.size() == 5);

        Ui::RegisterStockViewers(viewers);
    }

    ~ObxViewerFixture() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    ObxViewerFixture(const ObxViewerFixture&) = delete;
    ObxViewerFixture& operator=(const ObxViewerFixture&) = delete;

    Ui::Selection Pick(size_t root) const {
        Ui::Selection s;
        s.doc = doc;
        s.node.indices = {static_cast<uint32_t>(root)};
        return s;
    }

    Ui::UiContext Make() {
        return Ui::UiContext{workspace, nullptr, nullptr, nullptr, theme, commands, viewers, panels, clock, 0.0f};
    }

    // Pumps the job queue until `done` holds; false on timeout.
    bool PumpUntil(const std::function<bool()>& done) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!done()) {
            if (std::chrono::steady_clock::now() > deadline) return false;
            workspace.Jobs().Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    std::filesystem::path path;
    Modules::Workspace workspace;
    Services::Settings settings = Services::Settings::Empty();
    Ui::Theme theme{settings};
    Ui::CommandRegistry commands;
    Ui::ViewerRegistry viewers;
    Ui::PanelHost panels;
    Ui::FrameClock clock;
    Modules::DocumentId doc = 0;
};

// Draws `panel` in a fixed-size window for a few frames; the first frame of an auto-sized window
// draws nothing.
inline void DrawFrames(HeadlessImGui& imgui, Ui::IPanel& panel, Ui::UiContext& ui, int frames = 3) {
    for (int i = 0; i < frames; ++i) {
        imgui.NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(480, 320));
        ImGui::Begin("viewer");
        panel.Draw(ui);
        ImGui::End();
        imgui.EndFrame();
    }
}

}
