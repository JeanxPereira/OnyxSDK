#include <doctest/doctest.h>

#include <Onyx/Ui/Viewers/TextView.h>

#include "Support/ObxViewerFixture.h"

#include <Onyx/Ui/AssetPaths.h>
#include <Onyx/Ui/Fonts.h>

#include <string>

using namespace Onyx;
using namespace Onyx::Ui;

namespace {

TextView Make(const std::string& text) {
    return TextView(1, Modules::NodePath{{0}}, "t", Domain::Text{text, ""});
}

}

TEST_CASE("TextView splits the text into lines once") {
    CHECK(Make("").Lines() == 0);
    CHECK(Make("one").Lines() == 1);
    CHECK(Make("a\nb\nc").Lines() == 3);
    CHECK(Make("a\nb\nc\n").Lines() == 3);
    CHECK(Make("a\n\nc").Lines() == 3);

    TextView view = Make("first\r\nsecond\nthird");
    REQUIRE(view.Lines() == 3);
    CHECK(view.Line(0) == "first");
    CHECK(view.Line(1) == "second");
    CHECK(view.Line(2) == "third");
    CHECK(view.Line(3).empty());
}

TEST_CASE("TextView finds the next line containing a needle, ignoring case") {
    TextView view = Make("Alpha\nbeta\nGAMMA beta");
    CHECK(view.FindNext("beta", 0) == 1);
    CHECK(view.FindNext("beta", 2) == 2);
    CHECK(view.FindNext("gamma", 0) == 2);
    CHECK(view.FindNext("delta", 0) == view.Lines());
    CHECK(view.FindNext("", 0) == view.Lines());
}

TEST_CASE("TextView draws with and without word wrap") {
    HeadlessImGui imgui;
    TestSupport::ObxViewerFixture fx;
    UiContext ui = fx.Make();
    std::string text;
    for (int i = 0; i < 2000; ++i) text += "line " + std::to_string(i) + " with a few words to wrap around\n";
    TextView view = Make(text);

    TestSupport::DrawFrames(imgui, view, ui);
    view.wrap = true;
    TestSupport::DrawFrames(imgui, view, ui);
    CHECK(view.Lines() == 2000);
}

TEST_CASE("TextView draws in the host's mono font when the context names one") {
    HeadlessImGui imgui;
    TestSupport::ObxViewerFixture fx;
    auto fonts = BuildFonts(*ImGui::GetIO().Fonts, FindAsset("fonts/SFMono-Regular.otf"), 15.0f,
                            FindAsset("fonts/SFSymbols.ttf"));
    REQUIRE(fonts.ok());
    ImGui::GetIO().FontDefault = fonts.value().text;
    UiContext ui = fx.Make();
    TextView view = Make("mono\ntext");

    TestSupport::DrawFrames(imgui, view, ui);
    CHECK(fonts.value().mono->LastBaked == nullptr);

    ui.fonts = &fonts.value();
    TestSupport::DrawFrames(imgui, view, ui);
    CHECK(fonts.value().mono->LastBaked != nullptr);
}
