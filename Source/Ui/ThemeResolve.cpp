#include "Onyx/Ui/ThemeState.h"

#include <algorithm>

namespace Onyx::Ui {

ImGuiStyle HouseStyle() {
    ImGuiStyle s;

    s.WindowPadding = ImVec2(10.0f, 10.0f);
    s.FramePadding = ImVec2(4.0f, 3.0f);
    s.ItemSpacing = ImVec2(8.0f, 4.0f);
    s.CellPadding = ImVec2(4.0f, 2.0f);
    s.IndentSpacing = 21.0f;
    s.ScrollbarSize = 14.0f;

    // Square windows: a rounded OS window would clip badly in a viewport.
    s.WindowRounding = 0.0f;
    s.ChildRounding = 0.0f;
    s.FrameRounding = 5.0f;
    s.TabRounding = 5.0f;
    s.GrabRounding = 4.0f;

    s.WindowBorderSize = 1.0f;
    s.FrameBorderSize = 1.0f;

    return s;
}

ThemeResolved Resolve(const ThemeState& state, const ThemeEnvironment& env) {
    ThemeResolved r;

    const float userScale = std::clamp(state.userScale, kMinUserScale, kMaxUserScale);
    const float nativeScale = std::max(env.nativeScale, 0.1f);
    const float fontSizePt = std::clamp(state.fontSizePt, kMinFontSizePt, kMaxFontSizePt);

    r.globalScale = userScale * nativeScale;
    r.textBasePx = fontSizePt;
    r.fontScaleMain = userScale;
    r.fontScaleDpi = nativeScale;

    r.effectiveMode = (state.mode == ThemeMode::System)
                          ? (env.systemPrefersDark ? ThemeMode::Dark : ThemeMode::Light)
                          : state.mode;

    // Always rebuilt from the house style, never scaled incrementally, so
    // resolving twice equals resolving once.
    r.style = HouseStyle();
    if (userScale != 1.0f)
        r.style.ScaleAllSizes(userScale);
    r.style.SeparatorSize = std::max(r.style.SeparatorSize, 1.0f);

    // ScaleAllSizes leaves text alone: the drawn size is
    // FontSizeBase * FontScaleMain * FontScaleDpi, and FontSizeBase is an input.
    r.style.FontSizeBase = r.textBasePx;
    r.style.FontScaleMain = r.fontScaleMain;
    r.style.FontScaleDpi = r.fontScaleDpi;

    // An empty path means "never chosen" and resolves to the bundled default.
    r.atlasFontPath = state.fontPath.empty() ? env.defaultFontPath : state.fontPath;
    r.atlasRefPx = fontSizePt;

    ImVec4* palette = r.style.Colors;
    BuildPalette(palette, ImVec4(state.accent.x, state.accent.y, state.accent.z, state.accent.w),
                 r.effectiveMode);
    for (const ColorOverride& o : state.overrides) {
        if (const std::optional<int> index = ColorTokenIndex(o.token))
            palette[*index] = ImVec4(o.color.x, o.color.y, o.color.z, o.color.w);
    }

    return r;
}

}
