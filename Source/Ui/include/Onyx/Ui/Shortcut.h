#pragma once

#include <imgui.h>

#include <optional>
#include <string>
#include <string_view>

namespace Onyx::Ui {

struct Shortcut {
    ImGuiKeyChord chord = 0;
};

// Modifiers Ctrl, Shift, Alt, Super, then one key, joined by '+'. Case-insensitive. Key names are
// ImGui's key names; "," "." "-" "=" and "Plus" are accepted for Comma, Period, Minus, Equal and
// KeypadAdd. Needs a live ImGui context.
std::optional<Shortcut> ParseShortcut(std::string_view text);

// Canonical text: modifiers in the order Ctrl, Shift, Alt, Super, punctuation keys as symbols.
std::string FormatShortcut(Shortcut shortcut);

// True on the frame the chord is pressed, routed globally (ImGui::Shortcut).
bool ShortcutPressed(Shortcut shortcut);

}
