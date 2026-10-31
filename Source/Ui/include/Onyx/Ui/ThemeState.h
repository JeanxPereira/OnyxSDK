#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec4.hpp>
#include <imgui.h>

namespace Onyx::Ui {

enum class ThemeMode : uint8_t { Dark, Light, System };

inline constexpr float kMinUserScale = 0.5f;
inline constexpr float kMaxUserScale = 3.0f;
inline constexpr float kMinFontSizePt = 8.0f;
inline constexpr float kMaxFontSizePt = 72.0f;
inline constexpr float kThemeTransitionSeconds = 0.25f;

// A colour slot replaced by hand. `token` is the ImGui colour name without the
// ImGuiCol_ prefix, for example "TabHovered".
struct ColorOverride {
    std::string token;
    glm::vec4 color{};
    bool operator==(const ColorOverride&) const = default;
};

// The user-authored inputs. Plain, comparable, serialisable.
struct ThemeState {
    std::string fontPath;
    float fontSizePt = 15.0f;
    float userScale = 1.0f;
    glm::vec4 accent{0.282f, 0.173f, 0.398f, 1.0f};
    ThemeMode mode = ThemeMode::System;
    std::vector<ColorOverride> overrides;
    bool operator==(const ThemeState&) const = default;
};

// Measured by the host, never persisted.
struct ThemeEnvironment {
    float nativeScale = 1.0f;
    bool systemPrefersDark = true;
    std::string defaultFontPath;
    bool operator==(const ThemeEnvironment&) const = default;
};

// Everything derived from a ThemeState and a ThemeEnvironment.
struct ThemeResolved {
    float globalScale = 1.0f;
    float textBasePx = 0.0f;
    float fontScaleMain = 1.0f;
    float fontScaleDpi = 1.0f;
    ThemeMode effectiveMode = ThemeMode::Dark;
    ImGuiStyle style;
    std::string atlasFontPath;
    float atlasRefPx = 0.0f;
};

// House proportions in logical units: the base every Resolve starts from.
ImGuiStyle HouseStyle();

// Pure: no ImGui context, no globals.
ThemeResolved Resolve(const ThemeState& state, const ThemeEnvironment& env);

// Fills all ImGuiCol_COUNT entries for an accent and a mode.
void BuildPalette(ImVec4* colors, const ImVec4& accent, ThemeMode mode);

// Black or white, whichever reads on `surface` composited over `behind`.
ImVec4 ContrastColor(const ImVec4& surface, const ImVec4& behind);
ImVec4 ContrastColor(const ImVec4& opaqueSurface);

// `currentText` when it reads on `surface` over `windowBg`, otherwise black or white.
ImVec4 TextForSurface(const ImVec4& surface, const ImVec4& windowBg, const ImVec4& currentText);

float ThemeEaseOut(float t);

std::optional<int> ColorTokenIndex(std::string_view token);
std::string_view ColorToken(int imguiCol);

}
