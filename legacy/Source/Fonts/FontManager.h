#pragma once
#include "imgui.h"
#include <string>
#include <vector>

// ── Onyx Font Manager ───────────────────────────────────────────────────────
// Centralized font management inspired by ImHex's font_loader.cpp.
//
// Owns the font list, atlas build, icon font merge, and GPU upload.
// Replaces the scattered logic previously in SettingsWindow, TitleBar,
// WindowDecorator, and App.
//
// Usage:
//   Onyx::Fonts::Init();                        // enumerate system fonts
//   Onyx::Fonts::SetIconFont({...});             // configure icon font
//   Onyx::Fonts::BuildAtlas(fontIndex, size);    // build ImGui font atlas
//   // ... after ImGui::Render() ...
//   if (Onyx::Fonts::IsPendingRebuild())
//       Onyx::Fonts::UploadAtlas();              // GPU upload

namespace Onyx::Fonts {

// ── Font entry (system or bundled font) ───────────────────────────────────
struct FontEntry {
    std::string label;
    std::string path;          // empty = ImGui default (ProggyClean)
    bool        pixelPerfect = false;
};

// ── Icon font configuration ──────────────────────────────────────────────
struct IconFontConfig {
    std::string path;
    ImWchar     rangeStart = 0xE000;
    ImWchar     rangeEnd   = 0xFA19;
    ImVec2      glyphOffset = {0.0f, 3.0f};
};

// ── Init ──────────────────────────────────────────────────────────────────
void Init();                                // populate system font list
void SetIconFont(const IconFontConfig& cfg); // configure the icon font to merge

// ── Font list ─────────────────────────────────────────────────────────────
const std::vector<FontEntry>& GetFontList();
int FindFontIndex(const std::string& path); // returns -1 if not found
int DefaultFontIndex();                     // preferred default UI font (bundled SF Mono), else 0

// ── Atlas Build ───────────────────────────────────────────────────────────
// Builds the complete ImGui font atlas: base font + icon font merge.
// fontSizePx is the reference size (what the user picks in Settings). Since
// ImGui 1.92 the drawn size is FontSizeBase * FontScaleMain * FontScaleDpi,
// which ScaleManager drives -- do not pre-scale it here.
// Does NOT invalidate GPU resources — caller must call UploadAtlas() after.
void BuildAtlas(int fontIndex, float fontSizePx);

// ── GPU Upload ────────────────────────────────────────────────────────────
// imgui_impl_vulkan (T9) re-uploads a rebuilt atlas on its own, the next
// time it renders a frame's draw data -- see UploadAtlas()'s own comment
// in FontManager.cpp. Kept as a call site so BuildAtlas()'s "rebuild
// pending" flag still gets cleared somewhere outside the ImGui frame.
void UploadAtlas();

// ── Monospace font (always-on secondary atlas font) ──────────────────────
// Returned by BuildAtlas() each time it loads SFMono. May be nullptr on
// platforms / builds where the bundled mono font is missing; callers should
// fall back to the current font in that case (e.g. by passing nullptr to
// ImGui::PushFont, which leaves the current font active).
ImFont* GetMonoFont();

// ── State ─────────────────────────────────────────────────────────────────
bool   IsPendingRebuild();
void   RequestRebuild();
void   ClearPendingRebuild();
float  GetCurrentFontSize();
int    GetCurrentFontIndex();
const std::string& GetCurrentFontPath();

} // namespace Onyx::Fonts
