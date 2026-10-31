#include <doctest/doctest.h>

#include <PrismUi/PaletteView.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <memory>

#include "HeadlessImGui.h"
#include "Onyx/Shell/Shell.h"
#include "Support/ShellSupport.h"

using namespace Onyx;
using Onyx::TestSupport::TempDir;
using Onyx::TestSupport::TestOptions;

TEST_CASE("The palette view wraps its swatches inside the content region") {
    TempDir dir("palette_wrap");
    Shell::Shell shell(TestOptions(dir.path));
    auto palette = std::make_shared<Prism::Palette>();
    for (int i = 0; i < 16; ++i) {
        palette->colors.push_back({static_cast<uint8_t>(i * 16), 0, 0, 255});
    }
    Prism::Ui::PaletteView view(1, Modules::NodePath{{0}}, "palette", palette);
    CHECK(view.Count() == 16);

    HeadlessImGui rig;
    rig.NewFrame();
    Ui::UiContext ui = shell.Context();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(300, 400));
    REQUIRE(ImGui::Begin("palette", nullptr, ImGuiWindowFlags_NoScrollbar));
    view.Draw(ui);
    const ImGuiWindow* window = ImGui::GetCurrentWindow();
    CHECK(window->DC.CursorMaxPos.x <= window->WorkRect.Max.x + 0.5f);
    CHECK(window->DC.CursorMaxPos.y > window->WorkRect.Min.y);
    ImGui::End();
    rig.EndFrame();
}
