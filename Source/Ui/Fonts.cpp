#include "Onyx/Ui/Fonts.h"

#include "Onyx/Ui/AssetPaths.h"

namespace Onyx::Ui {

namespace {

constexpr ImWchar kSymbolRange[] = {0xE000, 0xF8FF, 0};
constexpr ImVec2 kSymbolOffset{0.0f, 3.0f};
constexpr const char* kMonoAsset = "fonts/SFMono-Regular.otf";

Error Missing(const std::filesystem::path& path, const char* what) {
    return Error{"ui.font.missing", std::string(what) + " not found: " + path.string(), 0};
}

}

Result<FontSet> BuildFonts(ImFontAtlas& atlas, const std::filesystem::path& textFont, float refPx,
                           const std::filesystem::path& iconFont) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(textFont, ec))
        return Result<FontSet>::Fail(Missing(textFont, "text font"));
    if (!std::filesystem::is_regular_file(iconFont, ec))
        return Result<FontSet>::Fail(Missing(iconFont, "icon font"));
    const std::filesystem::path monoFont = FindAsset(kMonoAsset);
    if (monoFont.empty())
        return Result<FontSet>::Fail(Missing(kMonoAsset, "mono font"));

    atlas.Clear();

    // The reference size is explicit on the destination so the merged symbols
    // can be explicit too; the scale factors live in the style, not the atlas.
    ImFontConfig textCfg;
    textCfg.SizePixels = refPx;
    ImFont* text = atlas.AddFontFromFileTTF(textFont.string().c_str(), refPx, &textCfg);
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
    if (!atlas.AddFontFromFileTTF(iconFont.string().c_str(), iconPx, &iconCfg, kSymbolRange))
        return Result<FontSet>::Fail(Missing(iconFont, "icon font (unreadable)"));

    ImFontConfig monoCfg;
    monoCfg.SizePixels = refPx;
    ImFont* mono = atlas.AddFontFromFileTTF(monoFont.string().c_str(), refPx, &monoCfg);
    if (!mono)
        return Result<FontSet>::Fail(Missing(monoFont, "mono font (unreadable)"));

    return Result<FontSet>::Ok(FontSet{text, mono, text});
}

}
