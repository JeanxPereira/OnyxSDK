#include <doctest/doctest.h>

#include <Onyx/Core/Vfs/MemoryFile.h>
#include <Onyx/Ui/Viewers/HexView.h>

#include "Support/ObxViewerFixture.h"

#include <Onyx/Ui/AssetPaths.h>
#include <Onyx/Ui/Fonts.h>

#include <memory>

using namespace Onyx;
using namespace Onyx::Ui;

namespace {

std::vector<std::byte> Ramp(size_t count) {
    std::vector<std::byte> out(count);
    for (size_t i = 0; i < count; ++i) out[i] = static_cast<std::byte>(i & 0xFF);
    return out;
}

}

TEST_CASE("HexView rows come from the bytes, sixteen to a row") {
    HexView view(1, Modules::NodePath{{0}}, "h", Ramp(40));
    CHECK(view.Size() == 40);
    CHECK(view.RowCount() == 3);

    const HexRow first = view.Row(0);
    CHECK(first.offset == 0);
    CHECK(first.hex == "00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F");
    CHECK(first.ascii == "................");

    const HexRow last = view.Row(2);
    CHECK(last.offset == 32);
    CHECK(last.hex == "20 21 22 23 24 25 26 27");
    CHECK(last.ascii == " !\"#$%&'");

    CHECK(view.Row(3).hex.empty());
}

TEST_CASE("HexView reads through a Reader slice without loading the blob") {
    auto file = std::make_shared<Vfs::MemoryFile>(Ramp(10000));
    HexView view(1, Modules::NodePath{{0}}, "h", Vfs::Reader(file, 100, 9000));
    CHECK(view.Size() == 9000);
    CHECK(view.RowCount() == 563);
    CHECK(view.Row(0).hex.substr(0, 5) == "64 65");
    const HexRow tail = view.Row(562);
    CHECK(tail.offset == 8992);
    CHECK(tail.hex.size() == 8 * 3 - 1);
    CHECK(view.Row(300).offset == 4800);
    CHECK(view.Row(300).hex.substr(0, 2) == "24");
}

TEST_CASE("HexView of nothing has no rows, and a long one draws") {
    HeadlessImGui imgui;
    TestSupport::ObxViewerFixture fx;
    UiContext ui = fx.Make();
    HexView empty(1, Modules::NodePath{{0}}, "h", std::vector<std::byte>{});
    CHECK(empty.RowCount() == 0);
    TestSupport::DrawFrames(imgui, empty, ui);

    HexView view(1, Modules::NodePath{{0}}, "h", Ramp(5000));
    view.GoTo(4000);
    TestSupport::DrawFrames(imgui, view, ui);
}

TEST_CASE("HexView draws in the host's mono font when the context names one") {
    HeadlessImGui imgui;
    TestSupport::ObxViewerFixture fx;
    auto fonts = BuildFonts(*ImGui::GetIO().Fonts, FindAsset("fonts/SFMono-Regular.otf"), 15.0f,
                            FindAsset("fonts/SFSymbols.ttf"));
    REQUIRE(fonts.ok());
    ImGui::GetIO().FontDefault = fonts.value().text;
    UiContext ui = fx.Make();
    HexView view(1, Modules::NodePath{{0}}, "h", Ramp(64));

    TestSupport::DrawFrames(imgui, view, ui);
    CHECK(fonts.value().mono->LastBaked == nullptr);

    ui.fonts = &fonts.value();
    TestSupport::DrawFrames(imgui, view, ui);
    CHECK(fonts.value().mono->LastBaked != nullptr);
}
