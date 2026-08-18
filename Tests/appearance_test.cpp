// ── Appearance resolution tests (doctest) ─────────────────────────────────
//
// Resolve() and HouseStyle() are pure, so the whole derivation is exercised
// here with no window, no GL context and no font atlas. Each case below is a
// defect that shipped before the module existed -- see
// docs/design/appearance-system.md.

#include <doctest/doctest.h>

#include <Onyx/Services/Appearance.h>

#include "imgui.h"

#include <cstring>

using namespace Onyx::Appearance;
using Onyx::Theme::ThemeMode;

namespace {

// BuildPalette reads no context state, but ImGui asserts on a null context in
// debug builds if anything reaches into it, so give the suite one.
ImGuiContext* EnsureContext() {
    if (!ImGui::GetCurrentContext())
        ImGui::CreateContext();
    return ImGui::GetCurrentContext();
}

State BaseState() {
    State s;
    s.fontPath   = "";
    s.fontSizePt = 14.0f;
    s.userScale  = 1.1f;
    s.accent     = ImVec4(0.88f, 0.15f, 0.15f, 1.0f);
    s.mode       = ThemeMode::Dark;
    return s;
}

Environment BaseEnv() {
    Environment e;
    e.nativeScale       = 1.0f;
    e.systemPrefersDark = true;
    e.defaultFontPath   = "bundled/SFMono-Regular.otf";
    return e;
}

// Metrics only -- colours are compared separately where it matters.
bool SameMetrics(const ImGuiStyle& a, const ImGuiStyle& b) {
    return a.WindowPadding.x == b.WindowPadding.x && a.WindowPadding.y == b.WindowPadding.y &&
           a.FramePadding.x == b.FramePadding.x && a.FramePadding.y == b.FramePadding.y &&
           a.ItemSpacing.x == b.ItemSpacing.x && a.ItemSpacing.y == b.ItemSpacing.y &&
           a.CellPadding.x == b.CellPadding.x && a.CellPadding.y == b.CellPadding.y &&
           a.IndentSpacing == b.IndentSpacing && a.ScrollbarSize == b.ScrollbarSize &&
           a.FrameRounding == b.FrameRounding && a.TabRounding == b.TabRounding &&
           a.GrabRounding == b.GrabRounding && a.WindowBorderSize == b.WindowBorderSize &&
           a.FrameBorderSize == b.FrameBorderSize;
}

} // namespace

TEST_CASE("Appearance::Resolve is deterministic and idempotent") {
    EnsureContext();
    const State s = BaseState();
    const Environment e = BaseEnv();

    const Resolved a = Resolve(s, e);
    const Resolved b = Resolve(s, e);

    CHECK(SameMetrics(a.style, b.style));
    CHECK(a.textBasePx == b.textBasePx);
    CHECK(a.globalScale == b.globalScale);
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        CHECK(a.style.Colors[i].x == b.style.Colors[i].x);
        CHECK(a.style.Colors[i].w == b.style.Colors[i].w);
    }

    // The old ApplyStyleScale read the live style and scaled it in place, so
    // repeated calls compounded. Building from HouseStyle every time cannot.
    State scaled = s;
    scaled.userScale = 2.0f;
    const Resolved once  = Resolve(scaled, e);
    const Resolved twice = Resolve(scaled, e);
    CHECK(SameMetrics(once.style, twice.style));
    CHECK(once.style.FramePadding.y == doctest::Approx(HouseStyle().FramePadding.y * 2.0f));
}

TEST_CASE("Appearance::Resolve keeps text size independent of UI scale") {
    EnsureContext();
    const Environment e = BaseEnv();
    State s = BaseState();
    s.fontSizePt = 18.0f;

    const Resolved at1 = Resolve(s, e);
    s.userScale = 2.5f;
    const Resolved at25 = Resolve(s, e);

    // The scale must not rewrite the chosen point size...
    CHECK(at1.textBasePx == doctest::Approx(18.0f));
    CHECK(at25.textBasePx == doctest::Approx(18.0f));
    CHECK(at25.atlasRefPx == doctest::Approx(18.0f));

    // ...it must arrive through the factor ImGui multiplies by, so the drawn
    // text grows with the widgets instead of staying at its reference size.
    CHECK(at25.fontScaleMain == doctest::Approx(2.5f));
    CHECK(at25.style.FontScaleMain == doctest::Approx(2.5f));
    CHECK(at25.style.FontSizeBase == doctest::Approx(18.0f));

    // Metrics did scale.
    CHECK(at25.style.FramePadding.y > at1.style.FramePadding.y);
}

TEST_CASE("Appearance::Resolve separates user scale from monitor scale") {
    EnsureContext();
    State s = BaseState();
    s.userScale = 1.5f;
    Environment e = BaseEnv();
    e.nativeScale = 2.0f;

    const Resolved r = Resolve(s, e);

    CHECK(r.globalScale == doctest::Approx(3.0f));
    CHECK(r.fontScaleMain == doctest::Approx(1.5f));   // user intent
    CHECK(r.fontScaleDpi == doctest::Approx(2.0f));    // monitor
    // Metrics follow the user factor only; the DPI factor reaches the geometry
    // through the backend's framebuffer scale, not by doubling the padding.
    // (Compared against the same user scale on a 1x monitor rather than against
    // a computed value -- ImGui's ScaleAllSizes truncates to whole pixels.)
    Environment oneToOne = e;
    oneToOne.nativeScale = 1.0f;
    const Resolved sameUserScale = Resolve(s, oneToOne);
    CHECK(r.style.FramePadding.y == sameUserScale.style.FramePadding.y);
    CHECK(r.style.WindowPadding.x == sameUserScale.style.WindowPadding.x);
    CHECK(r.style.FramePadding.y > HouseStyle().FramePadding.y);
}

TEST_CASE("Appearance::Resolve maps an unset font to the bundled default") {
    EnsureContext();
    const Environment e = BaseEnv();

    State unset = BaseState();
    unset.fontPath = "";
    CHECK(Resolve(unset, e).atlasFontPath == e.defaultFontPath);

    State chosen = BaseState();
    chosen.fontPath = "C:/Windows/Fonts/consola.ttf";
    CHECK(Resolve(chosen, e).atlasFontPath == "C:/Windows/Fonts/consola.ttf");
}

TEST_CASE("Appearance::Resolve resolves System mode from the environment") {
    EnsureContext();
    State s = BaseState();
    s.mode = ThemeMode::System;

    Environment dark = BaseEnv();
    dark.systemPrefersDark = true;
    CHECK(Resolve(s, dark).effectiveMode == ThemeMode::Dark);

    Environment light = BaseEnv();
    light.systemPrefersDark = false;
    CHECK(Resolve(s, light).effectiveMode == ThemeMode::Light);

    // An explicit choice ignores the OS.
    s.mode = ThemeMode::Light;
    CHECK(Resolve(s, dark).effectiveMode == ThemeMode::Light);
}

TEST_CASE("Appearance::Resolve applies colour overrides last") {
    EnsureContext();
    const Environment e = BaseEnv();
    State s = BaseState();

    const ImVec4 sentinel(0.123f, 0.456f, 0.789f, 1.0f);
    s.overrides.push_back({ImGuiCol_Button, sentinel});

    const Resolved r = Resolve(s, e);
    CHECK(r.style.Colors[ImGuiCol_Button].x == doctest::Approx(sentinel.x));
    CHECK(r.style.Colors[ImGuiCol_Button].y == doctest::Approx(sentinel.y));

    // An out-of-range slot is ignored rather than corrupting adjacent memory.
    State bad = BaseState();
    bad.overrides.push_back({ImGuiCol_COUNT + 5, sentinel});
    bad.overrides.push_back({-1, sentinel});
    CHECK_NOTHROW(Resolve(bad, e));
}

TEST_CASE("Appearance::Resolve clamps its inputs") {
    EnsureContext();
    const Environment e = BaseEnv();

    State tiny = BaseState();
    tiny.userScale  = 0.01f;
    tiny.fontSizePt = 1.0f;
    const Resolved r1 = Resolve(tiny, e);
    CHECK(r1.fontScaleMain == doctest::Approx(kMinScale));
    CHECK(r1.textBasePx == doctest::Approx(kMinFontSize));

    State huge = BaseState();
    huge.userScale  = 99.0f;
    huge.fontSizePt = 999.0f;
    const Resolved r2 = Resolve(huge, e);
    CHECK(r2.fontScaleMain == doctest::Approx(kMaxScale));
    CHECK(r2.textBasePx == doctest::Approx(kMaxFontSize));

    // Borders survive a small scale instead of rounding away to nothing.
    CHECK(r1.style.WindowBorderSize >= 0.0f);
    CHECK(r1.style.SeparatorSize >= 1.0f);
}

TEST_CASE("Appearance::State equality covers every input") {
    const State base = BaseState();

    CHECK(base == BaseState());

    State f = BaseState(); f.fontPath = "other.ttf";          CHECK(f != base);
    State z = BaseState(); z.fontSizePt = 15.0f;              CHECK(z != base);
    State u = BaseState(); u.userScale = 1.2f;                CHECK(u != base);
    State m = BaseState(); m.mode = ThemeMode::Light;         CHECK(m != base);
    State a = BaseState(); a.accent = ImVec4(0, 1, 0, 1);     CHECK(a != base);

    State o = BaseState();
    o.overrides.push_back({ImGuiCol_Text, ImVec4(1, 1, 1, 1)});
    CHECK(o != base);

    State o2 = o;
    CHECK(o2 == o);
    o2.overrides[0].color = ImVec4(0, 0, 0, 1);
    CHECK(o2 != o);
}
