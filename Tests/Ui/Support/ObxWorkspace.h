#pragma once

#include <OnyxBox/Module.h>
#include <OnyxBox/Writer.h>

#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/Core/Services/Settings.h>
#include <Onyx/Ui/Command.h>
#include <Onyx/Ui/FrameClock.h>
#include <Onyx/Ui/Panel.h>
#include <Onyx/Ui/PanelHost.h>
#include <Onyx/Ui/PathText.h>
#include <Onyx/Ui/Selection.h>
#include <Onyx/Ui/Theme.h>
#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/ViewerRegistry.h>
#include <Onyx/Ui/Viewers/StockViewers.h>

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "HeadlessImGui.h"
#include "TempDir.h"

// A workspace with the obx module registered and one document opened from a freshly written
// .obx. Its roots, in order: an 8x8 image "img", the three-line text "txt", a mesh "mesh", a 3x2
// table "table", a 40-byte blob "blob" (byte i is BlobByte(i)), then `flatChildren` 4-byte blobs
// named "flat<N>". `fileName` is UTF-8. The file is removed with the object.
struct ObxWorkspace {
    static constexpr size_t kImage = 0;
    static constexpr size_t kText = 1;
    static constexpr size_t kMesh = 2;
    static constexpr size_t kTable = 3;
    static constexpr size_t kBlob = 4;
    static constexpr size_t kBlobSize = 40;

    static std::byte BlobByte(size_t i) { return static_cast<std::byte>((i * 3 + 1) & 0xFF); }

    Onyx::Modules::Workspace workspace;
    Onyx::Modules::DocumentId doc = 0;
    std::filesystem::path file;

    ObxWorkspace(const std::filesystem::path& dir, size_t flatChildren, std::string_view fileName = "fixture.obx") {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        file = dir / Onyx::Ui::PathFromUtf8(fileName);

        std::vector<std::byte> pixels(8 * 8 * 4);
        for (size_t i = 0; i < pixels.size(); ++i) {
            pixels[i] = static_cast<std::byte>(i % 4 == 3 ? 255 : (i * 5) & 0xFF);
        }
        std::vector<std::byte> blob(kBlobSize);
        for (size_t i = 0; i < blob.size(); ++i) blob[i] = BlobByte(i);

        OnyxBox::Writer writer;
        writer.AddImage("img", 8, 8, pixels);
        writer.AddText("txt", "alpha\nbeta\ngamma");
        writer.AddMesh("mesh", {1, 1, 1, 1}, {0, 0, 0}, {1, 1, 1});
        writer.AddTable("table", {{"3", "three"}, {"1", "one"}, {"2", "two"}});
        writer.AddBlob("blob", blob);
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
        REQUIRE(workspace.Get(doc)->roots.size() == 5 + flatChildren);
    }

    ~ObxWorkspace() {
        std::error_code ec;
        std::filesystem::remove(file, ec);
    }

    ObxWorkspace(const ObxWorkspace&) = delete;
    ObxWorkspace& operator=(const ObxWorkspace&) = delete;
};

// A UiContext over an ObxWorkspace: no GPU, a theme over empty settings and real registries with
// the stock viewers registered. `workspace` and `doc` name the ObxWorkspace's own.
struct ObxUi {
    Onyx::Test::TempDir scope;
    ObxWorkspace obx;
    Onyx::Modules::Workspace& workspace = obx.workspace;
    const Onyx::Modules::DocumentId doc = obx.doc;
    Onyx::Services::Settings settings = Onyx::Services::Settings::Empty();
    Onyx::Ui::Theme theme{settings};
    Onyx::Ui::CommandRegistry commands;
    Onyx::Ui::ViewerRegistry viewers;
    Onyx::Ui::PanelHost panels;
    Onyx::Ui::FrameClock clock;

    // A document with the five roots only, in a scratch directory of its own.
    ObxUi() : ObxUi("fixture", 0) {}

    explicit ObxUi(const char* name, size_t flatChildren = 8, std::string_view fileName = "fixture.obx")
        : scope(std::string("ui-") + name), obx(scope.path(), flatChildren, fileName) {
        Onyx::Ui::RegisterStockViewers(viewers);
    }

    ObxUi(const ObxUi&) = delete;
    ObxUi& operator=(const ObxUi&) = delete;

    Onyx::Ui::UiContext Make() {
        return Onyx::Ui::UiContext{obx.workspace, nullptr, nullptr, nullptr, theme,
                                   commands,      viewers, panels,  clock,   0.0f};
    }

    Onyx::Ui::Selection Pick(size_t root) const {
        Onyx::Ui::Selection s;
        s.doc = obx.doc;
        s.node.indices = {static_cast<uint32_t>(root)};
        return s;
    }

    // Pumps the job queue until `done` holds; false after ten seconds.
    bool PumpUntil(const std::function<bool()>& done) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!done()) {
            if (std::chrono::steady_clock::now() > deadline) return false;
            obx.workspace.Jobs().Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
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

// Draws `panel` in a fixed-size, titled window for a few frames.
inline void DrawFrames(HeadlessImGui& rig, Onyx::Ui::IPanel& panel, Onyx::Ui::UiContext& ui, int frames = 3) {
    for (int i = 0; i < frames; ++i) {
        rig.NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(480, 320));
        ImGui::Begin("viewer");
        panel.Draw(ui);
        ImGui::End();
        rig.EndFrame();
    }
}
