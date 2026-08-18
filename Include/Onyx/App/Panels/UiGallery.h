#pragma once
#include <Onyx/App/IPanel.h>

#include <string>
#include <string_view>
#include <vector>

namespace Onyx::App {

// ── UI test mode ──────────────────────────────────────────────────────────
// A live catalogue of everything the Onyx UI is made of -- widget wrappers in
// each of their states, the full theme palette with a contrast audit, the font
// stack, the icon set, and the ImGui style vars -- so the look can be polished
// without opening a single asset.
//
// App registers it hidden; toggle from View -> UI Gallery or with F1. Nothing
// it changes is persisted: accent, scale, font and style edits live for the
// session only, so it is safe to wreck the theme while experimenting. Settings
// remains the place where changes are written to onyx.toml.
class UiGallery : public IPanel {
public:
    UiGallery() { visible = false; }

    void Draw() override;
    std::string_view getName() const override { return "UI Gallery"; }

private:
    void DrawGlobalBar();
    void DrawWidgetsPage();
    void DrawThemePage();
    void DrawTypographyPage();
    void DrawIconsPage();
    void DrawStylePage();
    void DrawDiagnosticsPage();

    // Frame the panel was last drawn on. A gap means it was hidden in between,
    // which is the cue to pull the window back in front -- panels get no
    // show/hide callback, and a test surface that opens behind the viewer is
    // useless.
    int m_lastDrawnFrame = -2;

    // Widgets page
    bool  m_treeSelected    = true;
    bool  m_toggleOn        = true;
    bool  m_checkbox        = true;
    int   m_radio           = 1;
    float m_slider          = 0.42f;
    float m_drag            = 12.0f;
    int   m_combo           = 0;
    char  m_text[128]       = "editable text";
    float m_progress        = 0.0f;

    // Theme page
    float m_minContrast     = 4.5f;   // WCAG AA for body text
    bool  m_onlyFailing     = false;

    // Typography page keeps no state: it renders Appearance::Get() directly.

    // Icons page. The match list is cached because the grid is ~6.7k entries:
    // re-filtering (and re-laying-out) all of them every frame would make the
    // one panel meant for judging UI smoothness the reason it stutters.
    std::vector<int> m_iconMatches;
    std::string      m_iconMatchesFor;
    bool             m_iconMatchesValid = false;

    char  m_iconFilter[64]  = "";
    float m_iconScale       = 2.0f;
    std::string m_copiedIcon;
    double m_copiedAt       = 0.0;

    // Diagnostics page
    bool m_showDemo         = false;
    bool m_showMetrics      = false;
    bool m_showStyleEditor  = false;
    bool m_showStackTool    = false;
    bool m_showGlyphDebug   = false;
};

} // namespace Onyx::App
