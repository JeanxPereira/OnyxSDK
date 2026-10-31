#include "Onyx/Ui/Shortcut.h"

#include <cctype>
#include <string_view>

namespace Onyx::Ui {

namespace {

struct Alias {
    std::string_view text;
    ImGuiKey key;
    bool canonical;
};

constexpr Alias kAliases[] = {
    {",", ImGuiKey_Comma, true},
    {".", ImGuiKey_Period, true},
    {"-", ImGuiKey_Minus, true},
    {"=", ImGuiKey_Equal, true},
    {"Plus", ImGuiKey_KeypadAdd, false},
};

struct Modifier {
    std::string_view name;
    ImGuiKeyChord flag;
};

constexpr Modifier kModifiers[] = {
    {"Ctrl", ImGuiMod_Ctrl},
    {"Shift", ImGuiMod_Shift},
    {"Alt", ImGuiMod_Alt},
    {"Super", ImGuiMod_Super},
};

bool EqualsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

std::optional<ImGuiKey> ParseKey(std::string_view name) {
    for (const Alias& alias : kAliases) {
        if (EqualsNoCase(alias.text, name)) return alias.key;
    }
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        const ImGuiKey key = static_cast<ImGuiKey>(k);
        const char* keyName = ImGui::GetKeyName(key);
        if (keyName && *keyName && EqualsNoCase(keyName, name)) return key;
    }
    return std::nullopt;
}

}

std::optional<Shortcut> ParseShortcut(std::string_view text) {
    ImGuiKeyChord mods = 0;
    ImGuiKey key = ImGuiKey_None;
    while (true) {
        const size_t plus = text.find('+');
        const std::string_view part = text.substr(0, plus);
        if (part.empty()) return std::nullopt;

        bool isMod = false;
        for (const Modifier& m : kModifiers) {
            if (EqualsNoCase(m.name, part)) {
                if (mods & m.flag) return std::nullopt;
                mods |= m.flag;
                isMod = true;
                break;
            }
        }
        if (!isMod) {
            if (plus != std::string_view::npos) return std::nullopt;
            const auto parsed = ParseKey(part);
            if (!parsed) return std::nullopt;
            key = *parsed;
        }
        if (plus == std::string_view::npos) break;
        text.remove_prefix(plus + 1);
    }
    if (key == ImGuiKey_None) return std::nullopt;
    return Shortcut{static_cast<ImGuiKeyChord>(mods | key)};
}

std::string FormatShortcut(Shortcut shortcut) {
    std::string out;
    for (const Modifier& m : kModifiers) {
        if (shortcut.chord & m.flag) {
            out += m.name;
            out += '+';
        }
    }
    const ImGuiKey key = static_cast<ImGuiKey>(shortcut.chord & ~ImGuiMod_Mask_);
    for (const Alias& alias : kAliases) {
        if (alias.canonical && alias.key == key) return out + std::string(alias.text);
    }
    const char* name = ImGui::GetKeyName(key);
    out += name ? name : "";
    return out;
}

bool ShortcutPressed(Shortcut shortcut) {
    return shortcut.chord != 0 && ImGui::Shortcut(shortcut.chord, ImGuiInputFlags_RouteGlobal);
}

}
