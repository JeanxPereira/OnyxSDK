#include <Onyx/Services/Appearance.h>

#include <Onyx/Services/Logger.h>
#include "Fonts/FontManager.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Onyx::Appearance {

// ── State comparison ──────────────────────────────────────────────────────

namespace {

bool SameColor(const ImVec4& a, const ImVec4& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

} // namespace

bool operator==(const State& a, const State& b) {
    if (a.fontPath != b.fontPath || a.fontSizePt != b.fontSizePt ||
        a.userScale != b.userScale || a.mode != b.mode || !SameColor(a.accent, b.accent))
        return false;
    if (a.overrides.size() != b.overrides.size())
        return false;
    for (size_t i = 0; i < a.overrides.size(); ++i) {
        if (a.overrides[i].imguiCol != b.overrides[i].imguiCol ||
            !SameColor(a.overrides[i].color, b.overrides[i].color))
            return false;
    }
    return true;
}

// ── HouseStyle ────────────────────────────────────────────────────────────

ImGuiStyle HouseStyle() {
    ImGuiStyle s;   // Dear ImGui defaults, then Onyx's proportions on top

    s.WindowPadding    = ImVec2(10.0f, 10.0f);
    s.FramePadding     = ImVec2(4.0f, 3.0f);
    s.ItemSpacing      = ImVec2(8.0f, 4.0f);
    s.CellPadding      = ImVec2(4.0f, 2.0f);
    s.IndentSpacing    = 21.0f;
    s.ScrollbarSize    = 14.0f;

    s.WindowRounding   = 0.0f;   // viewports: a rounded OS window would clip badly
    s.ChildRounding    = 0.0f;
    s.FrameRounding    = 5.0f;
    s.TabRounding      = 5.0f;
    s.GrabRounding     = 4.0f;

    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize  = 1.0f;

    return s;
}

// ── Resolve ───────────────────────────────────────────────────────────────

Resolved Resolve(const State& state, const Environment& env) {
    Resolved r;

    const float userScale   = std::clamp(state.userScale, kMinScale, kMaxScale);
    const float nativeScale = std::max(env.nativeScale, 0.1f);
    const float fontSizePt  = std::clamp(state.fontSizePt, kMinFontSize, kMaxFontSize);

    r.globalScale   = userScale * nativeScale;
    r.textBasePx    = fontSizePt;
    r.fontScaleMain = userScale;
    r.fontScaleDpi  = nativeScale;

    // System resolves here, from a value the caller measured -- Resolve itself
    // never asks the OS anything, which is what keeps it pure.
    r.effectiveMode = (state.mode == Theme::ThemeMode::System)
                          ? (env.systemPrefersDark ? Theme::ThemeMode::Dark
                                                   : Theme::ThemeMode::Light)
                          : state.mode;

    // ── Metrics ───────────────────────────────────────────────────────────
    // Built from the house style every time, never scaled incrementally, so
    // resolving twice gives the same answer as resolving once.
    r.style = HouseStyle();
    if (userScale != 1.0f)
        r.style.ScaleAllSizes(userScale);

    // Sub-pixel borders vanish; clamp the ones that would disappear at small
    // scales rather than letting the frame lose its outline.
    r.style.SeparatorSize    = std::max(r.style.SeparatorSize, 1.0f);
    r.style.ChildBorderSize  = std::max(r.style.ChildBorderSize, 0.0f);
    r.style.PopupBorderSize  = std::max(r.style.PopupBorderSize, 0.0f);
    r.style.FrameBorderSize  = std::max(r.style.FrameBorderSize, 0.0f);
    r.style.WindowBorderSize = std::max(r.style.WindowBorderSize, 0.0f);
    r.style.TabBorderSize    = std::max(r.style.TabBorderSize, 0.0f);

    // ── Type ──────────────────────────────────────────────────────────────
    // ScaleAllSizes does not touch text: since ImGui 1.92 the drawn size is
    // FontSizeBase * FontScaleMain * FontScaleDpi, with glyphs rasterised on
    // demand at the product. FontSizeBase is an input, not a scaled metric.
    r.style.FontSizeBase  = r.textBasePx;
    r.style.FontScaleMain = r.fontScaleMain;
    r.style.FontScaleDpi  = r.fontScaleDpi;

    // An empty path means "the user never chose", which must resolve to the
    // bundled default -- not to whichever entry happens to have a blank path.
    r.atlasFontPath = state.fontPath.empty() ? env.defaultFontPath : state.fontPath;
    r.atlasRefPx    = fontSizePt;

    // ── Colour ────────────────────────────────────────────────────────────
    Theme::BuildPalette(r.style.Colors, state.accent, r.effectiveMode);
    for (const ColorOverride& o : state.overrides) {
        if (o.imguiCol >= 0 && o.imguiCol < ImGuiCol_COUNT)
            r.style.Colors[o.imguiCol] = o.color;
    }

    return r;
}

// ── Live state ────────────────────────────────────────────────────────────

namespace {

State       s_desired;
State       s_applied;
Environment s_env;
bool        s_dirty        = true;   // first Commit always applies
bool        s_everApplied  = false;
bool        s_animateNext  = false;  // consumed by the next Commit

// What the atlas currently holds, so a scale change never triggers a rebake.
std::string s_bakedFontPath;
float       s_bakedRefPx = 0.0f;

} // namespace

const State&       Get() { return s_desired; }
const Environment& Env() { return s_env; }
const State&       Applied() { return s_applied; }

void Mutate(const std::function<void(State&)>& fn, bool animateColors) {
    if (!fn)
        return;
    fn(s_desired);
    s_dirty = true;
    s_animateNext = s_animateNext || animateColors;
}

void Set(const State& state) {
    s_desired = state;
    s_dirty   = true;
}

void SetEnvironment(const Environment& env) {
    s_env   = env;
    s_dirty = true;   // derived values depend on it
}

void Invalidate() { s_dirty = true; }

void Commit() {
    if (!s_dirty && s_everApplied && s_desired == s_applied)
        return;

    const Resolved r = Resolve(s_desired, s_env);

    // Whole-struct assignment: no read-modify-write of the live style, so no
    // drift and no dependence on what was applied before. The palette is the
    // exception -- ThemeManager still owns colour while it drives the ease-out
    // transition, so the live colours are carried across the assignment and
    // handed back to it below. (Folded in when the transition moves here.)
    ImGuiStyle& live = ImGui::GetStyle();
    ImVec4 liveColors[ImGuiCol_COUNT];
    std::memcpy(liveColors, live.Colors, sizeof(liveColors));

    live = r.style;
    if (s_everApplied)
        std::memcpy(live.Colors, liveColors, sizeof(liveColors));

    // The expensive path, gated on the only two inputs that can invalidate the
    // atlas. Moving the UI scale does not come through here.
    // Adopt whatever the font system already has, so taking ownership at
    // startup does not force a redundant rebake of an identical atlas.
    if (s_bakedRefPx <= 0.0f && Fonts::GetCurrentFontSize() > 0.0f) {
        s_bakedFontPath = Fonts::GetCurrentFontPath();
        s_bakedRefPx    = Fonts::GetCurrentFontSize();
    }

    const bool needsBake =
        (r.atlasFontPath != s_bakedFontPath) || (r.atlasRefPx != s_bakedRefPx);
    if (needsBake && !r.atlasFontPath.empty()) {
        const int index = Fonts::FindFontIndex(r.atlasFontPath);
        Fonts::BuildAtlas(index >= 0 ? index : Fonts::DefaultFontIndex(), r.atlasRefPx);

        // BuildAtlas re-seeds FontSizeBase from its own argument; Resolve is
        // the authority, so restore the resolved values after it.
        ImGuiStyle& live  = ImGui::GetStyle();
        live.FontSizeBase  = r.textBasePx;
        live.FontScaleMain = r.fontScaleMain;
        live.FontScaleDpi  = r.fontScaleDpi;

        s_bakedFontPath = r.atlasFontPath;
        s_bakedRefPx    = r.atlasRefPx;
    }

    // Must happen outside the ImGui frame: it destroys and recreates the font
    // texture the frame's draw commands would otherwise still reference.
    if (Fonts::IsPendingRebuild())
        Fonts::UploadAtlas();

    // ── Colour ────────────────────────────────────────────────────────────
    // Only when a colour input actually moved: ApplyTheme recomputes the whole
    // palette and, for ThemeMode::System, asks the OS -- neither belongs on the
    // path of a UI-scale drag.
    const bool colorsChanged =
        !s_everApplied || s_desired.accent.x != s_applied.accent.x ||
        s_desired.accent.y != s_applied.accent.y ||
        s_desired.accent.z != s_applied.accent.z ||
        s_desired.accent.w != s_applied.accent.w || s_desired.mode != s_applied.mode;
    if (colorsChanged)
        Theme::ApplyTheme(s_desired.accent, s_desired.mode, s_animateNext && s_everApplied);

    s_applied     = s_desired;
    s_dirty       = false;
    s_animateNext = false;
    s_everApplied = true;
}

} // namespace Onyx::Appearance
