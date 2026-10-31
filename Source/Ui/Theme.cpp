#include "Onyx/Ui/Theme.h"

#include "Onyx/Core/Services/Settings.h"
#include "Onyx/Core/Services/SettingsSchema.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Onyx::Ui {

namespace {

constexpr const char* kKeyMode = "ui.theme.mode";
constexpr const char* kKeyFontSize = "ui.theme.fontSizePt";
constexpr const char* kKeyUserScale = "ui.theme.userScale";
constexpr const char* kKeyFontPath = "ui.theme.fontPath";
constexpr const char* kKeyAccent = "ui.theme.accent";
constexpr const char* kKeyOverrides = "ui.theme.overrides";

std::string FormatColor(const glm::vec4& c) {
    auto byte = [](float v) {
        return static_cast<unsigned>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
    };
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "#%02x%02x%02x%02x", byte(c.x), byte(c.y), byte(c.z),
                  byte(c.w));
    return buffer;
}

// Accepts #rrggbb and #rrggbbaa.
std::optional<glm::vec4> ParseColor(std::string_view text) {
    if ((text.size() != 7 && text.size() != 9) || text.front() != '#')
        return std::nullopt;
    unsigned channel[4] = {0, 0, 0, 255};
    const size_t count = (text.size() - 1) / 2;
    for (size_t i = 0; i < count; ++i) {
        const std::string pair(text.substr(1 + i * 2, 2));
        for (char ch : pair) {
            if (!std::isxdigit(static_cast<unsigned char>(ch)))
                return std::nullopt;
        }
        channel[i] = static_cast<unsigned>(std::strtoul(pair.c_str(), nullptr, 16));
    }
    return glm::vec4(channel[0] / 255.0f, channel[1] / 255.0f, channel[2] / 255.0f,
                     channel[3] / 255.0f);
}

const char* ModeName(ThemeMode mode) {
    switch (mode) {
    case ThemeMode::Dark: return "dark";
    case ThemeMode::Light: return "light";
    case ThemeMode::System: break;
    }
    return "system";
}

std::string FormatOverrides(const std::vector<ColorOverride>& overrides) {
    std::string out;
    for (const ColorOverride& o : overrides) {
        if (!out.empty())
            out += ';';
        out += o.token;
        out += '=';
        out += FormatColor(o.color);
    }
    return out;
}

std::vector<ColorOverride> ParseOverrides(std::string_view text) {
    std::vector<ColorOverride> out;
    while (!text.empty()) {
        const size_t end = text.find(';');
        const std::string_view entry = text.substr(0, end);
        text = (end == std::string_view::npos) ? std::string_view() : text.substr(end + 1);
        const size_t eq = entry.find('=');
        if (eq == std::string_view::npos)
            continue;
        const std::string_view token = entry.substr(0, eq);
        const std::optional<glm::vec4> color = ParseColor(entry.substr(eq + 1));
        if (color && ColorTokenIndex(token))
            out.push_back(ColorOverride{std::string(token), *color});
    }
    return out;
}

ThemeState ReadState(const Services::Settings& settings) {
    ThemeState state;
    if (const auto mode = settings.GetString(kKeyMode)) {
        if (*mode == "dark")
            state.mode = ThemeMode::Dark;
        else if (*mode == "light")
            state.mode = ThemeMode::Light;
        else if (*mode == "system")
            state.mode = ThemeMode::System;
    }
    if (const auto v = settings.GetDouble(kKeyFontSize))
        state.fontSizePt = std::clamp(static_cast<float>(*v), kMinFontSizePt, kMaxFontSizePt);
    if (const auto v = settings.GetDouble(kKeyUserScale))
        state.userScale = std::clamp(static_cast<float>(*v), kMinUserScale, kMaxUserScale);
    if (const auto v = settings.GetString(kKeyFontPath))
        state.fontPath = *v;
    if (const auto v = settings.GetString(kKeyAccent)) {
        if (const auto color = ParseColor(*v))
            state.accent = *color;
    }
    if (const auto v = settings.GetString(kKeyOverrides))
        state.overrides = ParseOverrides(*v);
    return state;
}

}

Theme::Theme(Services::Settings& settings) : m_settings(settings), m_state(ReadState(settings)) {
    NoteAtlasInputs(Resolve(m_state, m_env));
}

void Theme::NoteAtlasInputs(const ThemeResolved& resolved) {
    if (resolved.atlasFontPath != m_atlasPath || resolved.atlasRefPx != m_atlasRefPx) {
        m_atlasPath = resolved.atlasFontPath;
        m_atlasRefPx = resolved.atlasRefPx;
        m_atlasDirty = true;
    }
}

void Theme::Mutate(const std::function<void(ThemeState&)>& fn, bool animateColors) {
    if (!fn)
        return;
    fn(m_state);
    m_dirty = true;
    m_animateNext = m_animateNext || animateColors;
    NoteAtlasInputs(Resolve(m_state, m_env));
}

void Theme::Set(const ThemeState& state) {
    m_state = state;
    m_dirty = true;
    NoteAtlasInputs(Resolve(m_state, m_env));
}

void Theme::SetEnvironment(const ThemeEnvironment& env) {
    m_env = env;
    m_dirty = true;
    NoteAtlasInputs(Resolve(m_state, m_env));
}

void Theme::Commit() {
    if (!m_dirty && m_hasApplied)
        return;

    ThemeResolved resolved = Resolve(m_state, m_env);
    NoteAtlasInputs(resolved);

    ImGuiStyle& live = ImGui::GetStyle();
    ImVec4 target[ImGuiCol_COUNT];
    std::memcpy(target, resolved.style.Colors, sizeof(target));

    // The transition starts from what is on screen, so interrupting one half
    // way continues from the colours the user is looking at.
    if (m_animateNext && m_hasApplied) {
        std::memcpy(m_from, live.Colors, sizeof(m_from));
        m_transitioning = true;
        m_elapsed = 0.0f;
    }
    if (m_transitioning) {
        const float t = m_elapsed / kThemeTransitionSeconds;
        if (t >= 1.0f) {
            m_transitioning = false;
        } else {
            const float e = ThemeEaseOut(t);
            for (int i = 0; i < ImGuiCol_COUNT; ++i) {
                const ImVec4& a = m_from[i];
                const ImVec4& b = target[i];
                resolved.style.Colors[i] = ImVec4(a.x + (b.x - a.x) * e, a.y + (b.y - a.y) * e,
                                                  a.z + (b.z - a.z) * e, a.w + (b.w - a.w) * e);
            }
        }
    }

    const bool changed =
        !m_hasApplied || std::memcmp(&resolved.style, &m_applied.style, sizeof(ImGuiStyle)) != 0;
    if (changed)
        live = resolved.style;

    m_applied = std::move(resolved);
    m_hasApplied = true;
    m_dirty = false;
    m_animateNext = false;
}

void Theme::Update(float dt) {
    if (!m_transitioning)
        return;
    m_elapsed += dt;
    m_dirty = true;
    Commit();
}

void Theme::Save() {
    m_settings.Set(kKeyMode, ModeName(m_state.mode));
    m_settings.Set(kKeyFontSize, static_cast<double>(m_state.fontSizePt));
    m_settings.Set(kKeyUserScale, static_cast<double>(m_state.userScale));
    m_settings.Set(kKeyFontPath, m_state.fontPath);
    m_settings.Set(kKeyAccent, FormatColor(m_state.accent));
    m_settings.Set(kKeyOverrides, FormatOverrides(m_state.overrides));
}

void Theme::DescribeSettings(Services::SettingsSchema& schema) {
    const ThemeState defaults;
    schema.Enum(kKeyMode, {"dark", "light", "system"}, 2, "Theme")
        .Double(kKeyFontSize, defaults.fontSizePt, kMinFontSizePt, kMaxFontSizePt, "Font size")
        .Double(kKeyUserScale, defaults.userScale, kMinUserScale, kMaxUserScale, "UI scale")
        .String(kKeyFontPath, defaults.fontPath, "Font file")
        .String(kKeyAccent, FormatColor(defaults.accent), "Accent #rrggbb");
}

}
