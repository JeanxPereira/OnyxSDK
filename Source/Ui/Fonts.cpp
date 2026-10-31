#include "Onyx/Ui/Fonts.h"

#include "Onyx/Ui/AssetPaths.h"
#include "Onyx/Ui/PathText.h"
#include "Onyx/Ui/Theme.h"

#include <Onyx/Core/Services/Log.h>

namespace Onyx::Ui {

namespace {

// The ceiling is the highest code point SfSymbols.h defines (U+FA19).
constexpr ImWchar kSymbolRange[] = {0xE000, 0xFA19, 0};
constexpr ImVec2 kSymbolOffset{0.0f, 3.0f};
constexpr const char* kMonoAsset = "fonts/SFMono-Regular.otf";
constexpr const char* kTextAsset = "fonts/SFMono-Regular.otf";
constexpr const char* kSymbolAsset = "fonts/SFSymbols.ttf";

Error Missing(const std::filesystem::path& path, const char* what) {
    return Error{"ui.font.missing", std::string(what) + " not found: " + PathToUtf8(path), 0};
}

}

Result<FontSet> BuildFonts(ImFontAtlas& atlas, const std::filesystem::path& textFont, float refPx,
                           const std::filesystem::path& iconFont,
                           const std::filesystem::path& monoFontOverride) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(textFont, ec))
        return Result<FontSet>::Fail(Missing(textFont, "text font"));
    if (!std::filesystem::is_regular_file(iconFont, ec))
        return Result<FontSet>::Fail(Missing(iconFont, "icon font"));
    const std::filesystem::path monoFont =
        monoFontOverride.empty() ? FindAsset(kMonoAsset) : monoFontOverride;
    if (monoFont.empty() || !std::filesystem::is_regular_file(monoFont, ec))
        return Result<FontSet>::Fail(
            Missing(monoFont.empty() ? kMonoAsset : monoFont, "mono font"));

    atlas.Clear();

    // The reference size is explicit on the destination so the merged symbols
    // can be explicit too; the scale factors live in the style, not the atlas.
    ImFontConfig textCfg;
    textCfg.SizePixels = refPx;
    ImFont* text = atlas.AddFontFromFileTTF(PathToUtf8(textFont).c_str(), refPx, &textCfg);
    if (!text)
        return Result<FontSet>::Fail(Missing(textFont, "text font (unreadable)"));

    // Minimum advance only: clamping the maximum too would shift the ink off
    // the box ImGui measures. ImGui scales both values with the drawn size.
    const float iconPx = refPx + 1.0f;
    ImFontConfig iconCfg;
    iconCfg.MergeMode = true;
    iconCfg.PixelSnapH = true;
    iconCfg.GlyphMinAdvanceX = iconPx;
    iconCfg.GlyphOffset = kSymbolOffset;
    if (!atlas.AddFontFromFileTTF(PathToUtf8(iconFont).c_str(), iconPx, &iconCfg, kSymbolRange))
        return Result<FontSet>::Fail(Missing(iconFont, "icon font (unreadable)"));

    ImFontConfig monoCfg;
    monoCfg.SizePixels = refPx;
    ImFont* mono = atlas.AddFontFromFileTTF(PathToUtf8(monoFont).c_str(), refPx, &monoCfg);
    if (!mono)
        return Result<FontSet>::Fail(Missing(monoFont, "mono font (unreadable)"));

    return Result<FontSet>::Ok(FontSet{text, mono, text});
}

Result<FontSet> BuildThemeFonts(const Theme& theme, ImFontAtlas& atlas) {
    const ThemeResolved& applied = theme.Applied();
    std::filesystem::path text = PathFromUtf8(applied.atlasFontPath);
    std::error_code ec;
    if (text.empty() || !std::filesystem::is_regular_file(text, ec)) {
        if (!text.empty()) {
            Services::Log::Default().Write(Services::LogLevel::Warn, "ui.fonts",
                                           "font not found, using the bundled one: {}",
                                           applied.atlasFontPath);
        }
        text = FindAsset(kTextAsset);
    }
    const std::filesystem::path icons = FindAsset(kSymbolAsset);

    Result<FontSet> fonts =
        Result<FontSet>::Fail({"ui.font.missing", "the theme has no atlas size yet", 0});
    if (text.empty() || icons.empty()) {
        const char* missing = text.empty() ? kTextAsset : kSymbolAsset;
        fonts = Result<FontSet>::Fail(Missing(missing, "bundled font"));
    } else if (applied.atlasRefPx > 0.0f) {
        fonts = BuildFonts(atlas, text, applied.atlasRefPx, icons);
    }
    if (!fonts.ok()) {
        atlas.Clear();
        atlas.AddFontDefault();
    }
    return fonts;
}

}
