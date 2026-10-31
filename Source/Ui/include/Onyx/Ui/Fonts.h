#pragma once

#include <filesystem>

#include <imgui.h>

#include "Onyx/Core/Result.h"

namespace Onyx::Ui {

class Theme;

struct FontSet {
    ImFont* text = nullptr;
    ImFont* mono = nullptr;
    ImFont* icons = nullptr;
};

// Clears the atlas and rebuilds it: the text font with the SF Symbols range
// merged in, then the bundled mono font. `icons` is the text font itself: the
// symbols are merged glyphs of it. An empty `monoFontOverride` means the bundled one.
// Fails with "ui.font.missing" when a file is absent.
Result<FontSet> BuildFonts(ImFontAtlas& atlas, const std::filesystem::path& textFont, float refPx,
                           const std::filesystem::path& iconFont,
                           const std::filesystem::path& monoFontOverride = {});

// The atlas a theme asks for: BuildFonts over the theme's applied font (Applied().atlasFontPath,
// UTF-8) at Applied().atlasRefPx with the bundled symbols and mono font. A font path that names no
// file logs a warning through Log::Default() and falls back to the bundled text font (FindAsset).
// When even the bundled fonts are missing, or the theme has not committed, it fails with
// ui.font.missing and leaves the atlas holding ImGui's default font only. Call it after
// Theme::Commit when AtlasDirty() is set, then rebuild the renderer's font textures.
Result<FontSet> BuildThemeFonts(const Theme& theme, ImFontAtlas& atlas);

}
