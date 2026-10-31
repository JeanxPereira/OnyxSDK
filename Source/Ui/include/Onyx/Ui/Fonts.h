#pragma once

#include <filesystem>

#include <imgui.h>

#include "Onyx/Core/Result.h"

namespace Onyx::Ui {

struct FontSet {
    ImFont* text = nullptr;
    ImFont* mono = nullptr;
    ImFont* icons = nullptr;
};

// Clears the atlas and rebuilds it: the text font with the SF Symbols range
// merged in, then the bundled mono font. `icons` is the text font itself: the
// symbols are merged glyphs of it. Fails with "ui.font.missing" when a file is absent.
Result<FontSet> BuildFonts(ImFontAtlas& atlas, const std::filesystem::path& textFont, float refPx,
                           const std::filesystem::path& iconFont);

}
