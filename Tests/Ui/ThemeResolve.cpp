#include <doctest/doctest.h>

#include "Onyx/Ui/Theme.h"
#include "Onyx/Ui/ThemeState.h"
#include "HeadlessImGui.h"

#include <cstring>

#include "Onyx/Core/Services/Settings.h"

using namespace Onyx::Ui;

namespace {

ThemeState SampleState() {
    ThemeState s;
    s.fontSizePt = 17.0f;
    s.userScale = 1.5f;
    s.accent = glm::vec4(0.2f, 0.5f, 0.8f, 1.0f);
    s.mode = ThemeMode::Dark;
    s.overrides.push_back({"TabHovered", glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)});
    return s;
}

bool SameBytes(const ImGuiStyle& a, const ImGuiStyle& b) {
    return std::memcmp(&a, &b, sizeof(ImGuiStyle)) == 0;
}

bool SameColors(const ImGuiStyle& a, const ImGuiStyle& b) {
    return std::memcmp(a.Colors, b.Colors, sizeof(a.Colors)) == 0;
}

}

TEST_CASE("Resolve is deterministic") {
    const ThemeState state = SampleState();
    const ThemeEnvironment env;
    const ThemeResolved first = Resolve(state, env);
    (void)Resolve(ThemeState{}, ThemeEnvironment{2.0f, false, "x"});
    const ThemeResolved second = Resolve(state, env);
    CHECK(SameBytes(first.style, second.style));
    CHECK(first.atlasFontPath == second.atlasFontPath);
    CHECK(first.atlasRefPx == second.atlasRefPx);
}

TEST_CASE("Resolve is idempotent through Commit") {
    HeadlessImGui rig;
    Onyx::Services::Settings settings = Onyx::Services::Settings::Empty();
    Theme theme(settings);
    theme.Set(SampleState());
    theme.Commit();
    const ImGuiStyle once = ImGui::GetStyle();
    theme.Set(SampleState());
    theme.Commit();
    CHECK(SameBytes(once, ImGui::GetStyle()));
}

TEST_CASE("Mutating userScale leaves the other inputs alone") {
    const ThemeState before = SampleState();
    ThemeState after = before;
    after.userScale = 2.5f;
    CHECK(after.fontSizePt == before.fontSizePt);
    CHECK(after.accent == before.accent);
    CHECK(after.overrides == before.overrides);

    const ThemeResolved a = Resolve(before, ThemeEnvironment{});
    const ThemeResolved b = Resolve(after, ThemeEnvironment{});
    CHECK(a.textBasePx == b.textBasePx);
    CHECK(a.atlasRefPx == b.atlasRefPx);
    CHECK(SameColors(a.style, b.style));
}

TEST_CASE("Scaling law") {
    ThemeState one = SampleState();
    one.userScale = 1.0f;
    ThemeState two = one;
    two.userScale = 2.0f;
    const ThemeResolved a = Resolve(one, ThemeEnvironment{});
    const ThemeResolved b = Resolve(two, ThemeEnvironment{});

    CHECK(b.style.WindowPadding.x == doctest::Approx(a.style.WindowPadding.x * 2.0f));
    CHECK(b.style.ItemSpacing.y == doctest::Approx(a.style.ItemSpacing.y * 2.0f));
    CHECK(b.style.ScrollbarSize == doctest::Approx(a.style.ScrollbarSize * 2.0f));
    CHECK(b.fontScaleMain == two.userScale);
    CHECK(b.textBasePx == two.fontSizePt);
    CHECK(b.style.FontSizeBase == two.fontSizePt);
    CHECK(b.style.FontScaleMain == two.userScale);

    const ThemeResolved dpi = Resolve(one, ThemeEnvironment{1.5f, true, {}});
    CHECK(dpi.fontScaleDpi == 1.5f);
    CHECK(dpi.globalScale == doctest::Approx(1.5f));
}

TEST_CASE("An empty font path resolves to the bundled default") {
    ThemeEnvironment env;
    env.defaultFontPath = "assets/fonts/SFMono-Regular.otf";
    ThemeState state;
    CHECK(Resolve(state, env).atlasFontPath == env.defaultFontPath);
    state.fontPath = "other.ttf";
    CHECK(Resolve(state, env).atlasFontPath == "other.ttf");
}

TEST_CASE("System mode follows the environment") {
    ThemeState state;
    state.mode = ThemeMode::System;
    CHECK(Resolve(state, ThemeEnvironment{1.0f, true, {}}).effectiveMode == ThemeMode::Dark);
    CHECK(Resolve(state, ThemeEnvironment{1.0f, false, {}}).effectiveMode == ThemeMode::Light);
}

TEST_CASE("Overrides land on their token") {
    const ThemeResolved r = Resolve(SampleState(), ThemeEnvironment{});
    const ImVec4 c = r.style.Colors[ImGuiCol_TabHovered];
    CHECK(c.x == 1.0f);
    CHECK(c.y == 0.0f);
}

TEST_CASE("Colour tokens map through ImGui names") {
    CHECK(ColorTokenIndex("TabHovered") == ImGuiCol_TabHovered);
    CHECK_FALSE(ColorTokenIndex("NoSuchColour").has_value());
    CHECK(ColorToken(ImGuiCol_TabHovered) == "TabHovered");
}

TEST_CASE("Contrast helpers pick a readable colour") {
    CHECK(ContrastColor(ImVec4(0, 0, 0, 1)).x == 1.0f);
    CHECK(ContrastColor(ImVec4(1, 1, 1, 1)).x < 0.1f);
    const ImVec4 white(1, 1, 1, 1);
    const ImVec4 bg(0.1f, 0.1f, 0.1f, 1);
    CHECK(TextForSurface(ImVec4(0.1f, 0.1f, 0.1f, 1), bg, white).x == 1.0f);
    CHECK(TextForSurface(ImVec4(1, 1, 1, 1), bg, white).x < 0.1f);
}

TEST_CASE("Commit with no change assigns nothing") {
    HeadlessImGui rig;
    Onyx::Services::Settings settings = Onyx::Services::Settings::Empty();
    Theme theme(settings);
    theme.Set(SampleState());
    theme.Commit();

    ImGui::GetStyle().Alpha = 0.123f;
    const ImGuiStyle before = ImGui::GetStyle();
    theme.Commit();
    CHECK(SameBytes(before, ImGui::GetStyle()));

    theme.Mutate([](ThemeState&) {});
    theme.Commit();
    CHECK(ImGui::GetStyle().Alpha == 0.123f);
}

TEST_CASE("The atlas is dirty only for font inputs") {
    HeadlessImGui rig;
    Onyx::Services::Settings settings = Onyx::Services::Settings::Empty();
    Theme theme(settings);
    theme.Commit();
    CHECK(theme.AtlasDirty());
    theme.ClearAtlasDirty();

    theme.Mutate([](ThemeState& s) { s.userScale = 2.0f; });
    theme.Mutate([](ThemeState& s) { s.accent = glm::vec4(0.9f, 0.1f, 0.1f, 1.0f); });
    theme.Commit();
    CHECK_FALSE(theme.AtlasDirty());

    theme.Mutate([](ThemeState& s) { s.fontSizePt = 20.0f; });
    CHECK(theme.AtlasDirty());
    theme.ClearAtlasDirty();

    theme.Mutate([](ThemeState& s) { s.fontPath = "other.ttf"; });
    CHECK(theme.AtlasDirty());
    theme.ClearAtlasDirty();

    ThemeEnvironment env;
    env.defaultFontPath = "fallback.otf";
    theme.Mutate([](ThemeState& s) { s.fontPath.clear(); });
    theme.ClearAtlasDirty();
    theme.SetEnvironment(env);
    CHECK(theme.AtlasDirty());
}

TEST_CASE("An animated colour change eases to the target") {
    HeadlessImGui rig;
    Onyx::Services::Settings settings = Onyx::Services::Settings::Empty();
    Theme theme(settings);
    theme.Set(SampleState());
    theme.Commit();
    const ImVec4 start = ImGui::GetStyle().Colors[ImGuiCol_Button];

    theme.Mutate([](ThemeState& s) { s.accent = glm::vec4(0.9f, 0.9f, 0.1f, 1.0f); }, true);
    theme.Commit();
    CHECK(theme.IsTransitioning());
    CHECK(ImGui::GetStyle().Colors[ImGuiCol_Button].x == doctest::Approx(start.x));

    theme.Update(0.1f);
    CHECK(theme.IsTransitioning());
    theme.Update(0.2f);
    CHECK_FALSE(theme.IsTransitioning());

    const ThemeResolved target = Resolve(theme.State(), theme.Env());
    CHECK(SameColors(target.style, ImGui::GetStyle()));
}

TEST_CASE("A committed font size survives the next frame") {
    HeadlessImGui rig;
    Onyx::Services::Settings settings = Onyx::Services::Settings::Empty();
    Theme theme(settings);
    theme.Commit();
    const auto frame = [&] {
        rig.NewFrame();
        ImGui::Begin("probe");
        ImGui::TextUnformatted("probe");
        ImGui::End();
        rig.EndFrame();
    };
    frame();
    frame();

    theme.Mutate([](ThemeState& s) { s.fontSizePt = 22.0f; });
    theme.Commit();
    float inFrame = 0.0f;
    rig.NewFrame();
    ImGui::Begin("probe");
    inFrame = ImGui::GetStyle().FontSizeBase;
    ImGui::End();
    rig.EndFrame();
    CHECK(inFrame == doctest::Approx(22.0f));
    CHECK(ImGui::GetStyle().FontSizeBase == doctest::Approx(22.0f));
}
