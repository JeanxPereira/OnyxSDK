#include "Onyx/Ui/ThemeState.h"

#include <algorithm>
#include <cmath>

namespace Onyx::Ui {

namespace {

ImVec4 Alpha(ImVec4 c, float a) {
    return ImVec4(c.x, c.y, c.z, a);
}

ImVec4 Dim(ImVec4 c, float f) {
    return ImVec4(c.x * f, c.y * f, c.z * f, c.w);
}

ImVec4 Lerp4(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                  a.w + (b.w - a.w) * t);
}

// BT.709 relative luminance of an opaque colour.
float Luminance(const ImVec4& c) {
    return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
}

float BlendedLuminance(const ImVec4& fg, const ImVec4& bgOpaque) {
    const float a = fg.w;
    return Luminance(ImVec4((1.0f - a) * bgOpaque.x + a * fg.x, (1.0f - a) * bgOpaque.y + a * fg.y,
                            (1.0f - a) * bgOpaque.z + a * fg.z, 1.0f));
}

// An accent surface over bgOpaque that keeps at least targetDelta luminance
// distance from the background, starting from baseAlpha.
ImVec4 TintSurface(const ImVec4& accent, const ImVec4& bgOpaque, float targetDelta,
                   float baseAlpha) {
    const ImVec4 candidate = Alpha(accent, baseAlpha);
    const float bgLum = Luminance(bgOpaque);
    if (std::fabs(BlendedLuminance(candidate, bgOpaque) - bgLum) >= targetDelta)
        return candidate;

    // Blended luminance is bgLum + a * (accentLum - bgLum); solve for a.
    const float lumDiff = Luminance(accent) - bgLum;
    float solvedAlpha;
    if (std::fabs(lumDiff) < 0.001f) {
        solvedAlpha = 0.95f;
    } else {
        const float headroomUp = 1.0f - bgLum;
        const float headroomDown = bgLum;
        float targetLum;
        if (lumDiff > 0)
            targetLum = (headroomUp >= targetDelta) ? (bgLum + targetDelta) : (bgLum - targetDelta);
        else
            targetLum = (headroomDown >= targetDelta) ? (bgLum - targetDelta) : (bgLum + targetDelta);
        solvedAlpha = (targetLum - bgLum) / lumDiff;
    }
    return Alpha(accent, std::clamp(solvedAlpha, 0.10f, 0.95f));
}

// Mixes `color` toward white or black until |Lum(color) - Lum(ref)| >= minDelta.
ImVec4 ForceMinLumDelta(const ImVec4& color, const ImVec4& ref, float minDelta) {
    const float refLum = Luminance(ref);
    if (std::fabs(Luminance(color) - refLum) >= minDelta)
        return color;

    const ImVec4 target = (1.0f - refLum >= refLum) ? ImVec4(1.0f, 1.0f, 1.0f, color.w)
                                                    : ImVec4(0.0f, 0.0f, 0.0f, color.w);
    float lo = 0.0f;
    float hi = 1.0f;
    for (int i = 0; i < 16; ++i) {
        const float mid = (lo + hi) * 0.5f;
        if (std::fabs(Luminance(Lerp4(color, target, mid)) - refLum) >= minDelta)
            hi = mid;
        else
            lo = mid;
    }
    ImVec4 result = Lerp4(color, target, hi);
    result.w = color.w;
    return result;
}

// Keeps a pure-accent indicator visible against the window background.
ImVec4 NudgeAccentIndicator(const ImVec4& accent, const ImVec4& bgOpaque, float threshold = 0.05f) {
    if (std::fabs(Luminance(accent) - Luminance(bgOpaque)) > threshold)
        return accent;
    if (Luminance(bgOpaque) > 0.5f)
        return Lerp4(accent, ImVec4(0.0f, 0.0f, 0.0f, accent.w), 0.40f);
    return Lerp4(accent, ImVec4(1.0f, 1.0f, 1.0f, accent.w), 0.40f);
}

}

ImVec4 ContrastColor(const ImVec4& surface, const ImVec4& behind) {
    const float a = surface.w;
    const float r = (1.0f - a) * behind.x + a * surface.x;
    const float g = (1.0f - a) * behind.y + a * surface.y;
    const float b = (1.0f - a) * behind.z + a * surface.z;
    const float luminance = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    return luminance > 0.45f ? ImVec4(0.05f, 0.05f, 0.05f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

ImVec4 ContrastColor(const ImVec4& opaqueSurface) {
    const ImVec4 opaque(opaqueSurface.x, opaqueSurface.y, opaqueSurface.z, 1.0f);
    return ContrastColor(opaque, opaque);
}

ImVec4 TextForSurface(const ImVec4& surface, const ImVec4& windowBg, const ImVec4& currentText) {
    const float a = surface.w;
    const float r = (1.0f - a) * windowBg.x + a * surface.x;
    const float g = (1.0f - a) * windowBg.y + a * surface.y;
    const float b = (1.0f - a) * windowBg.z + a * surface.z;
    const float surfaceLum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    const float textLum = Luminance(currentText);

    // Only flip when the surface would clash with the theme's own text colour.
    constexpr float kMinDiff = 0.40f;
    if (std::fabs(surfaceLum - textLum) >= kMinDiff)
        return currentText;
    return (surfaceLum > 0.5f) ? ImVec4(0.05f, 0.05f, 0.05f, 1.0f) : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

float ThemeEaseOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

void BuildPalette(ImVec4* colors, const ImVec4& accent, ThemeMode mode) {
    ImGuiStyle base;
    if (mode == ThemeMode::Light)
        ImGui::StyleColorsLight(&base);
    else
        ImGui::StyleColorsDark(&base);
    for (int i = 0; i < ImGuiCol_COUNT; ++i)
        colors[i] = base.Colors[i];

    const ImVec4 bg = colors[ImGuiCol_WindowBg];

    constexpr float kDeltaNormal = 0.06f;
    constexpr float kDeltaHovered = 0.12f;
    constexpr float kDeltaActive = 0.20f;

    colors[ImGuiCol_FrameBg] = TintSurface(accent, bg, kDeltaNormal, 0.15f);
    colors[ImGuiCol_FrameBgHovered] = TintSurface(accent, bg, kDeltaHovered, 0.40f);
    colors[ImGuiCol_FrameBgActive] = TintSurface(accent, bg, kDeltaActive, 0.67f);

    colors[ImGuiCol_Tab] = TintSurface(accent, bg, kDeltaNormal, 0.30f);
    colors[ImGuiCol_TabHovered] = TintSurface(accent, bg, kDeltaHovered, 0.80f);
    colors[ImGuiCol_TabSelected] = TintSurface(accent, bg, kDeltaActive, 0.60f);
    colors[ImGuiCol_TabDimmed] = TintSurface(accent, bg, kDeltaNormal, 0.15f);
    colors[ImGuiCol_TabDimmedSelected] = TintSurface(accent, bg, kDeltaHovered, 0.40f);
    colors[ImGuiCol_TabSelectedOverline] = NudgeAccentIndicator(accent, bg);
    colors[ImGuiCol_TabDimmedSelectedOverline] = Alpha(NudgeAccentIndicator(accent, bg), 0.50f);

    colors[ImGuiCol_Button] = TintSurface(accent, bg, kDeltaNormal, 0.40f);
    colors[ImGuiCol_ButtonHovered] = TintSurface(accent, bg, kDeltaHovered, 0.80f);
    colors[ImGuiCol_ButtonActive] = TintSurface(Dim(accent, 0.9f), bg, kDeltaActive, 0.90f);

    colors[ImGuiCol_Header] = TintSurface(accent, bg, kDeltaNormal, 0.31f);
    colors[ImGuiCol_HeaderHovered] = TintSurface(accent, bg, kDeltaHovered, 0.80f);
    colors[ImGuiCol_HeaderActive] = TintSurface(accent, bg, kDeltaActive, 0.90f);

    colors[ImGuiCol_Separator] = TintSurface(accent, bg, kDeltaNormal, 0.40f);
    colors[ImGuiCol_SeparatorHovered] = TintSurface(accent, bg, kDeltaHovered, 0.78f);
    colors[ImGuiCol_SeparatorActive] = TintSurface(accent, bg, kDeltaActive, 0.90f);

    colors[ImGuiCol_ResizeGrip] = TintSurface(accent, bg, kDeltaNormal, 0.20f);
    colors[ImGuiCol_ResizeGripHovered] = TintSurface(accent, bg, kDeltaHovered, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = TintSurface(accent, bg, kDeltaActive, 0.90f);

    colors[ImGuiCol_SliderGrab] = TintSurface(accent, bg, kDeltaHovered, 0.80f);
    colors[ImGuiCol_SliderGrabActive] = NudgeAccentIndicator(accent, bg);

    colors[ImGuiCol_ScrollbarGrab] = TintSurface(accent, bg, kDeltaNormal, 0.40f);
    colors[ImGuiCol_ScrollbarGrabHovered] = TintSurface(accent, bg, kDeltaHovered, 0.70f);
    colors[ImGuiCol_ScrollbarGrabActive] = TintSurface(accent, bg, kDeltaActive, 0.90f);

    colors[ImGuiCol_TitleBgActive] = TintSurface(accent, bg, kDeltaNormal, 0.30f);
    colors[ImGuiCol_TextSelectedBg] = TintSurface(accent, bg, kDeltaNormal, 0.35f);
    colors[ImGuiCol_DragDropTarget] = TintSurface(accent, bg, kDeltaActive, 0.90f);
    colors[ImGuiCol_DockingPreview] = TintSurface(accent, bg, kDeltaHovered, 0.70f);

    colors[ImGuiCol_CheckMark] = NudgeAccentIndicator(accent, bg);
    colors[ImGuiCol_NavCursor] = NudgeAccentIndicator(accent, bg);
    colors[ImGuiCol_InputTextCursor] = NudgeAccentIndicator(accent, bg);

    colors[ImGuiCol_CheckboxSelectedBg] =
        Lerp4(colors[ImGuiCol_FrameBg], colors[ImGuiCol_FrameBgHovered], 0.65f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.50f);

    colors[ImGuiCol_Text] = ContrastColor(colors[ImGuiCol_WindowBg]);
    colors[ImGuiCol_TextDisabled] = Lerp4(colors[ImGuiCol_Text], colors[ImGuiCol_WindowBg], 0.50f);
    colors[ImGuiCol_TextLink] = ForceMinLumDelta(accent, colors[ImGuiCol_Text], 0.15f);

    // ImGui draws every label with the one global text colour, so each accent
    // surface is held on the window background's side of the luminance range
    // (about 3:1 against that text). Pure-accent indicators carry no text and
    // are exempt.
    constexpr float kSurfLumMaxDark = 0.30f;
    constexpr float kSurfLumMinLight = 0.70f;

    const bool isDark = Luminance(colors[ImGuiCol_Text]) > 0.5f;
    const float surfMax = isDark ? kSurfLumMaxDark : 1.0f;
    const float surfMin = isDark ? 0.0f : kSurfLumMinLight;

    static constexpr int kAccentSurfaceSlots[] = {
        ImGuiCol_FrameBg,           ImGuiCol_FrameBgHovered,      ImGuiCol_FrameBgActive,
        ImGuiCol_Tab,               ImGuiCol_TabHovered,          ImGuiCol_TabSelected,
        ImGuiCol_TabDimmed,         ImGuiCol_TabDimmedSelected,   ImGuiCol_Button,
        ImGuiCol_ButtonHovered,     ImGuiCol_ButtonActive,        ImGuiCol_Header,
        ImGuiCol_HeaderHovered,     ImGuiCol_HeaderActive,        ImGuiCol_Separator,
        ImGuiCol_SeparatorHovered,  ImGuiCol_SeparatorActive,     ImGuiCol_ResizeGrip,
        ImGuiCol_ResizeGripHovered, ImGuiCol_ResizeGripActive,    ImGuiCol_SliderGrab,
        ImGuiCol_TitleBgActive,     ImGuiCol_ScrollbarGrab,       ImGuiCol_ScrollbarGrabHovered,
        ImGuiCol_ScrollbarGrabActive, ImGuiCol_DockingPreview,    ImGuiCol_TextSelectedBg,
        ImGuiCol_DragDropTarget,
    };

    // Binary-search the largest alpha whose blended surface stays inside the bound.
    auto clampSurfaceAlpha = [&](ImVec4& surface) {
        const float curLum = BlendedLuminance(surface, bg);
        if (curLum >= surfMin && curLum <= surfMax)
            return;

        // A window background outside the bound cannot be helped.
        const float bgLum = Luminance(bg);
        if (bgLum < surfMin || bgLum > surfMax)
            return;

        float lo = 0.0f;
        float hi = surface.w;
        float best = 0.0f;
        for (int i = 0; i < 24; ++i) {
            const float mid = (lo + hi) * 0.5f;
            const float trialLum = BlendedLuminance(Alpha(surface, mid), bg);
            if (trialLum >= surfMin && trialLum <= surfMax) {
                best = mid;
                lo = mid;
            } else {
                hi = mid;
            }
        }
        surface = Alpha(surface, best);
    };

    for (int slot : kAccentSurfaceSlots)
        clampSurfaceAlpha(colors[slot]);
}

std::optional<int> ColorTokenIndex(std::string_view token) {
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        if (token == ImGui::GetStyleColorName(i))
            return i;
    }
    return std::nullopt;
}

std::string_view ColorToken(int imguiCol) {
    if (imguiCol < 0 || imguiCol >= ImGuiCol_COUNT)
        return {};
    return ImGui::GetStyleColorName(imguiCol);
}

}
