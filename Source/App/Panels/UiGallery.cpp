#define IMGUI_DEFINE_MATH_OPERATORS
#include <Onyx/App/Panels/UiGallery.h>

#include <Onyx/App/Widgets.h>
#include <Onyx/Fonts/IconTable.h>
#include <Onyx/Fonts/SFSymbols.h>
#include <Onyx/Services/ThemeManager.h>

#include "App/FontDebuggerWindow.h"
#include "Fonts/FontManager.h"
#include "Services/ScaleManager.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Onyx::App {
namespace {

// ── Local helpers ─────────────────────────────────────────────────────────

// Bordered, rounded group with its own title -- the same shape SettingsWindow
// uses, kept local so the gallery does not depend on that panel's internals.
//
// MUST be paired with EndSection() unconditionally, even when this returns
// false: unlike Begin/End for windows, ImGui requires EndChild() on every
// BeginChild(), and a section returns false as soon as it is scrolled out of
// view. Skipping it leaves a child on the window stack and the next End()
// asserts with "Must call EndChild() and not End()!".
//
//     if (BeginSection("Title")) { ...contents... }
//     EndSection();
bool BeginSection(const char* label) {
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
    const bool open = ImGui::BeginChild(label, ImVec2(0, 0),
                                        ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
                                        ImGuiWindowFlags_MenuBar);
    if (open && ImGui::BeginMenuBar()) {
        ImGui::TextUnformatted(label);
        ImGui::EndMenuBar();
    }
    ImGui::PopStyleVar();
    return open;
}

void EndSection() {
    ImGui::EndChild();
    ImGui::Spacing();
}

void Hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

// Label in the left column, widgets in the right -- keeps every row of the
// widgets page aligned no matter how wide the labels get.
bool BeginRowTable(const char* id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit))
        return false;
    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn("##widgets", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void RowLabel(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
}

// ── WCAG contrast ─────────────────────────────────────────────────────────
// The theme's own TextForSurface picks text colours by plain luminance
// distance. The audit below uses the real WCAG formula (gamma-expanded
// luminance, 0.05 offset) so the numbers match what a contrast checker would
// report back to a user.

float Linearise(float c) {
    return (c <= 0.03928f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float RelativeLuminance(const ImVec4& c) {
    return 0.2126f * Linearise(c.x) + 0.7152f * Linearise(c.y) + 0.0722f * Linearise(c.z);
}

// Resolves a translucent colour over its backdrop before measuring -- half the
// ImGui palette is alpha-blended, and measuring it raw lies.
ImVec4 Flatten(const ImVec4& fg, const ImVec4& bg) {
    const float a = fg.w;
    return ImVec4((1.0f - a) * bg.x + a * fg.x,
                  (1.0f - a) * bg.y + a * fg.y,
                  (1.0f - a) * bg.z + a * fg.z,
                  1.0f);
}

float ContrastRatio(const ImVec4& a, const ImVec4& b) {
    const float la = RelativeLuminance(a), lb = RelativeLuminance(b);
    const float hi = (la > lb) ? la : lb;
    const float lo = (la > lb) ? lb : la;
    return (hi + 0.05f) / (lo + 0.05f);
}

const char* kComboItems[] = {"Alpha", "Beta", "Gamma", "Delta"};

} // namespace

// ── Panel ─────────────────────────────────────────────────────────────────

void UiGallery::Draw() {
    const int frame = ImGui::GetFrameCount();
    if (frame != m_lastDrawnFrame + 1)
        ImGui::SetNextWindowFocus();
    m_lastDrawnFrame = frame;

    // Centred by default: the engine's own panels claim the corners, and a test
    // surface that opens underneath the viewer is the first thing you have to
    // fight before you can use it.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                   vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(920, 640), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("UI Gallery", &visible)) {
        ImGui::End();
        return;
    }

    DrawGlobalBar();

    if (ImGui::BeginTabBar("##gallery_pages")) {
        if (ImGui::BeginTabItem(ICON_SF_SQUARE_GRID_3X3 " Widgets")) {
            DrawWidgetsPage();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_SF_PAINTBRUSH " Theme")) {
            DrawThemePage();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_SF_TEXTFORMAT " Typography")) {
            DrawTypographyPage();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_SF_SPARKLES " Icons")) {
            DrawIconsPage();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_SF_WRENCH_AND_SCREWDRIVER " Style")) {
            DrawStylePage();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_SF_QUESTIONMARK_CIRCLE " Diagnostics")) {
            DrawDiagnosticsPage();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();

    // Owned sub-windows -- drawn outside Begin/End so they are top level.
    if (m_showDemo)       ImGui::ShowDemoWindow(&m_showDemo);
    if (m_showMetrics)    ImGui::ShowMetricsWindow(&m_showMetrics);
    if (m_showStackTool)  ImGui::ShowIDStackToolWindow(&m_showStackTool);
    if (m_showGlyphDebug) FontDebuggerWindow::Draw(&m_showGlyphDebug);
    if (m_showStyleEditor) {
        if (ImGui::Begin("Dear ImGui Style Editor", &m_showStyleEditor))
            ImGui::ShowStyleEditor();
        ImGui::End();
    }
}

// ── Global bar ────────────────────────────────────────────────────────────
// The one control that changes how every other page looks, so it sits above
// the tab bar instead of being buried inside Typography.

void UiGallery::DrawGlobalBar() {
    float uiScale = Scale::GetUserScale();   // read live: Settings can move it too

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("UI scale");
    ImGui::SameLine();

    ImGui::SetNextItemWidth(220.0f);
    bool changed = ImGui::SliderFloat("##global_ui_scale", &uiScale, 0.5f, 3.0f, "%.2fx");

    const float presets[] = {1.0f, 1.25f, 1.5f, 2.0f};
    for (float preset : presets) {
        ImGui::SameLine();
        ImGui::PushID(int(preset * 100));
        char label[16];
        std::snprintf(label, sizeof(label), "%.2gx", double(preset));
        if (Widgets::SmallButton(label)) {
            uiScale = preset;
            changed = true;
        }
        ImGui::PopID();
    }

    if (changed) {
        Scale::SetUserScale(uiScale);
        Scale::ApplyStyleScale(uiScale);
        Theme::ApplyTheme(Theme::GetAccent());  // the style reset drops colours
    }

    ImGui::SameLine();
    ImGui::TextDisabled("| text %.1f px", double(ImGui::GetFontSize()));

    ImGui::Separator();
}

// ── Page: Widgets ─────────────────────────────────────────────────────────

void UiGallery::DrawWidgetsPage() {
    ImGui::Spacing();
    Hint("Every Onyx widget wrapper next to the plain ImGui one it replaces. The "
         "wrappers repaint their label through Theme::TextForSurface, so push the "
         "accent to a bright colour on the Theme page and watch which column stays "
         "readable.");
    ImGui::Spacing();

    if (BeginSection("Buttons")) {
        if (BeginRowTable("##buttons")) {
            RowLabel("Onyx");
            Widgets::Button("Button");
            ImGui::SameLine();
            Widgets::SmallButton("SmallButton");
            ImGui::SameLine();
            Widgets::IconButton("##gear", ICON_SF_GEAR, {ImVec2(0, 0), "Plain icon button"});
            ImGui::SameLine();
            Widgets::IconButton("##star", ICON_SF_STAR_FILL,
                                {ImVec2(0, 0), "Selected (toggle look)", m_toggleOn});
            ImGui::SameLine();
            Widgets::IconButton("##trash", ICON_SF_TRASH, {ImVec2(0, 0), "Disabled", false, true});
            ImGui::SameLine();
            if (Widgets::Button("Flip selected"))
                m_toggleOn = !m_toggleOn;

            RowLabel("ImGui");
            ImGui::Button("Button");
            ImGui::SameLine();
            ImGui::SmallButton("SmallButton");
            ImGui::SameLine();
            ImGui::BeginDisabled();
            ImGui::Button("Disabled");
            ImGui::EndDisabled();

            ImGui::EndTable();
        }
    }
    EndSection();

    if (BeginSection("Selection")) {
        if (BeginRowTable("##selection")) {
            RowLabel("Selectable");
            Widgets::Selectable("Unselected", false, 0, ImVec2(130, 0));
            ImGui::SameLine();
            Widgets::Selectable("Selected", true, 0, ImVec2(130, 0));

            RowLabel("Tree node");
            const ImGuiTreeNodeFlags leaf =
                ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            Widgets::ColoredTreeNode("##tree_a", "Mesh", ICON_SF_CUBE_FILL,
                                     ImVec4(0.45f, 0.80f, 1.00f, 1.0f), leaf, false);
            Widgets::ColoredTreeNode("##tree_b", "Texture (selected)", ICON_SF_PHOTO,
                                     ImVec4(1.00f, 0.72f, 0.35f, 1.0f), leaf, m_treeSelected);
            if (ImGui::IsItemClicked())
                m_treeSelected = !m_treeSelected;
            Widgets::ColoredTreeNode("##tree_c", "Audio", ICON_SF_WAVEFORM,
                                     ImVec4(0.60f, 1.00f, 0.60f, 1.0f), leaf, false);

            RowLabel("Header");
            if (Widgets::CollapsingHeader("Onyx CollapsingHeader"))
                ImGui::TextDisabled("   ...contents...");

            ImGui::EndTable();
        }
    }
    EndSection();

    if (BeginSection("Tabs and menus")) {
        if (ImGui::BeginTabBar("##sample_tabs")) {
            if (Widgets::BeginTabItem("Overview")) {
                ImGui::TextDisabled("Onyx tab item.");
                ImGui::EndTabItem();
            }
            if (Widgets::BeginTabItem("Details")) {
                ImGui::TextDisabled("Second tab.");
                ImGui::EndTabItem();
            }
            if (Widgets::BeginTabItem("Raw")) {
                ImGui::TextDisabled("Third tab.");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::Spacing();
        if (Widgets::Button("Open sample menu"))
            ImGui::OpenPopup("##sample_menu");
        if (ImGui::BeginPopup("##sample_menu")) {
            Widgets::MenuItem("Enabled item", "Ctrl+E");
            Widgets::MenuItem("Checked item", nullptr, true);
            Widgets::MenuItem("Disabled item", nullptr, false, false);
            ImGui::Separator();
            Widgets::MenuItem("Another", "Ctrl+A");
            ImGui::EndPopup();
        }
    }
    EndSection();

    if (BeginSection("Inputs")) {
        if (BeginRowTable("##inputs")) {
            ImGui::PushItemWidth(240.0f);

            RowLabel("Slider");
            ImGui::SliderFloat("##slider", &m_slider, 0.0f, 1.0f, "%.2f");

            RowLabel("Drag");
            ImGui::DragFloat("##drag", &m_drag, 0.1f, 0.0f, 100.0f, "%.1f px");

            RowLabel("Text");
            ImGui::InputText("##text", m_text, IM_ARRAYSIZE(m_text));

            RowLabel("Combo");
            ImGui::Combo("##combo", &m_combo, kComboItems, IM_ARRAYSIZE(kComboItems));

            RowLabel("Toggles");
            ImGui::Checkbox("Checkbox", &m_checkbox);
            ImGui::SameLine();
            ImGui::RadioButton("A", &m_radio, 0);
            ImGui::SameLine();
            ImGui::RadioButton("B", &m_radio, 1);

            ImGui::PopItemWidth();
            ImGui::EndTable();
        }
    }
    EndSection();

    if (BeginSection("Feedback")) {
        m_progress += ImGui::GetIO().DeltaTime * 0.25f;
        if (m_progress > 1.0f)
            m_progress = 0.0f;

        if (BeginRowTable("##feedback")) {
            RowLabel("Progress");
            ImGui::ProgressBar(m_progress, ImVec2(240, 0));

            RowLabel("Text");
            ImGui::TextUnformatted("Normal");
            ImGui::SameLine();
            ImGui::TextDisabled("Disabled");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.40f, 0.40f, 1.0f), "Error");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.80f, 0.30f, 1.0f), "Warning");

            RowLabel("Table");
            if (ImGui::BeginTable("##sample_table", 3,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Type");
                ImGui::TableSetupColumn("Size");
                ImGui::TableHeadersRow();

                struct Row { const char* name; const char* type; const char* size; };
                static const Row rows[] = {
                    {"hero_body",    "Mesh",    "128 KB"},
                    {"hero_diffuse", "Texture", "2.0 MB"},
                    {"footstep_01",  "Audio",   "44 KB"},
                };
                for (const Row& r : rows) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(r.name);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(r.type);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(r.size);
                }
                ImGui::EndTable();
            }
            ImGui::EndTable();
        }
    }
    EndSection();
}

// ── Page: Theme ───────────────────────────────────────────────────────────

void UiGallery::DrawThemePage() {
    ImGui::Spacing();

    if (BeginSection("Accent and mode")) {
        ImVec4 accent = Theme::GetAccent();
        ImGui::PushItemWidth(200.0f);
        if (ImGui::ColorEdit3("Accent", &accent.x, ImGuiColorEditFlags_NoInputs))
            Theme::ApplyTheme(accent, /*animate=*/false);

        ImGui::SameLine();
        const char* modes[] = {"Dark", "Light", "System"};
        int mode = int(Theme::GetMode());
        if (ImGui::Combo("Mode", &mode, modes, IM_ARRAYSIZE(modes)))
            Theme::ApplyTheme(accent, Theme::ThemeMode(mode), /*animate=*/true);
        ImGui::PopItemWidth();

        ImGui::SameLine();
        ImGui::TextDisabled("(resolves to %s)", Theme::GetEffectiveMode() == Theme::ThemeMode::Light
                                                    ? "Light"
                                                    : "Dark");

        // Quick jumps to the accents that historically broke label contrast.
        ImGui::Spacing();
        struct Preset { const char* name; ImVec4 color; };
        static const Preset presets[] = {
            {"Onyx red",   {0.88f, 0.15f, 0.15f, 1.0f}},
            {"Neon green", {0.20f, 1.00f, 0.40f, 1.0f}},
            {"Hot pink",   {1.00f, 0.25f, 0.70f, 1.0f}},
            {"Near white", {0.95f, 0.95f, 0.95f, 1.0f}},
            {"Near black", {0.06f, 0.06f, 0.08f, 1.0f}},
        };
        for (const Preset& p : presets) {
            ImGui::PushID(p.name);
            if (ImGui::ColorButton("##swatch", p.color, ImGuiColorEditFlags_NoTooltip,
                                   ImVec2(20, 20)))
                Theme::ApplyTheme(p.color, /*animate=*/true);
            ImGui::PopID();
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(p.name);
            ImGui::SameLine(0.0f, 16.0f);
        }
        ImGui::NewLine();
        Hint("Session-only -- Settings is what writes onyx.toml.");
    }
    EndSection();

    if (BeginSection("Toolbar tokens")) {
        struct Token { const char* name; ImVec4 color; };
        const Token tokens[] = {
            {"ToolbarButton",       Theme::ToolbarButton()},
            {"ToolbarButtonHover",  Theme::ToolbarButtonHover()},
            {"ToolbarButtonActive", Theme::ToolbarButtonActive()},
            {"ToolbarButtonText",   Theme::ToolbarButtonText()},
        };
        for (const Token& t : tokens) {
            ImGui::ColorButton(t.name, t.color, ImGuiColorEditFlags_None, ImVec2(20, 20));
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(t.name);
            ImGui::SameLine(0.0f, 16.0f);
        }
        ImGui::NewLine();
    }
    EndSection();

    if (BeginSection("Palette and contrast audit")) {
        ImGui::PushItemWidth(150.0f);
        ImGui::SliderFloat("Minimum ratio", &m_minContrast, 1.0f, 7.0f, "%.1f:1");
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::Checkbox("Only failing", &m_onlyFailing);
        ImGui::SameLine();
        if (Widgets::Button("Clear overrides"))
            Theme::ClearAllOverrides();
        Hint("4.5:1 is WCAG AA for body text, 3:1 for large text and UI edges. Each row "
             "paints the real (alpha-resolved) surface and puts the label colour "
             "TextForSurface would pick on top of it.");

        const ImVec4 windowBg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        const ImVec4 textCol  = ImGui::GetStyleColorVec4(ImGuiCol_Text);

        for (const Theme::ColorGroup& group : Theme::GetColorGroups()) {
            if (!ImGui::TreeNodeEx(group.groupName, ImGuiTreeNodeFlags_DefaultOpen))
                continue;

            if (ImGui::BeginTable(group.groupName, 4,
                                  ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                ImGui::TableSetupColumn("Colour", ImGuiTableColumnFlags_WidthFixed, 190.0f);
                ImGui::TableSetupColumn("Edit", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                ImGui::TableSetupColumn("Label on surface", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Ratio", ImGuiTableColumnFlags_WidthFixed, 80.0f);

                for (const Theme::ColorEntry& entry : group.entries) {
                    ImVec4 color = ImGui::GetStyleColorVec4(entry.imguiColIdx);
                    const ImVec4 surface = Flatten(color, windowBg);
                    const ImVec4 picked  = Theme::TextForSurface(color);
                    const float ratio    = ContrastRatio(picked, surface);
                    const bool fails     = ratio < m_minContrast;

                    if (m_onlyFailing && !fails)
                        continue;

                    ImGui::TableNextRow();
                    ImGui::PushID(entry.imguiColIdx);

                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(entry.displayName);
                    if (Theme::HasOverride(entry.imguiColIdx)) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("(override)");
                    }

                    ImGui::TableNextColumn();
                    if (ImGui::ColorEdit4("##edit", &color.x,
                                          ImGuiColorEditFlags_NoInputs |
                                              ImGuiColorEditFlags_NoLabel))
                        Theme::SetColorOverride(entry.imguiColIdx, color);

                    ImGui::TableNextColumn();
                    const ImVec2 p0 = ImGui::GetCursorScreenPos();
                    const ImVec2 sz(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
                    ImGui::GetWindowDrawList()->AddRectFilled(p0, ImVec2(p0.x + sz.x, p0.y + sz.y),
                                                              ImGui::GetColorU32(surface), 3.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text, picked);
                    ImGui::SetCursorScreenPos(
                        ImVec2(p0.x + 8.0f, p0.y + ImGui::GetStyle().FramePadding.y));
                    ImGui::TextUnformatted("The quick brown fox");
                    ImGui::PopStyleColor();
                    ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + sz.y));

                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextColored(fails ? ImVec4(1.00f, 0.45f, 0.35f, 1.0f)
                                             : ImVec4(0.45f, 0.90f, 0.55f, 1.0f),
                                       "%.2f:1", double(ratio));

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::TreePop();
        }

        ImGui::Spacing();
        ImGui::Text("Window text on window background: %.2f:1",
                    double(ContrastRatio(textCol, windowBg)));
    }
    EndSection();
}

// ── Page: Typography ──────────────────────────────────────────────────────

void UiGallery::DrawTypographyPage() {
    ImGui::Spacing();

    if (m_fontSize <= 0.0f) {
        m_fontSize  = Fonts::GetCurrentFontSize();
        m_fontIndex = Fonts::GetCurrentFontIndex();
    }

    if (BeginSection("Font")) {
        const std::vector<Fonts::FontEntry>& fonts = Fonts::GetFontList();
        if (m_fontIndex < 0 || m_fontIndex >= int(fonts.size()))
            m_fontIndex = Fonts::DefaultFontIndex();

        ImGui::PushItemWidth(260.0f);
        if (!fonts.empty() && m_fontIndex >= 0 &&
            ImGui::BeginCombo("Family", fonts[m_fontIndex].label.c_str())) {
            for (int i = 0; i < int(fonts.size()); ++i) {
                if (ImGui::Selectable(fonts[i].label.c_str(), i == m_fontIndex) &&
                    i != m_fontIndex) {
                    m_fontIndex = i;
                    Fonts::BuildAtlas(m_fontIndex, m_fontSize);
                }
            }
            ImGui::EndCombo();
        }

        // Rebuilding the atlas mid-drag would thrash the GPU upload, so the
        // rebuild waits for the slider to be released (same as Settings does).
        if (ImGui::SliderFloat("Size", &m_fontSize, 8.0f, 32.0f, "%.0f px"))
            m_fontSizeDirty = true;
        if (m_fontSizeDirty && ImGui::IsItemDeactivatedAfterEdit()) {
            m_fontSizeDirty = false;
            Fonts::BuildAtlas(m_fontIndex, m_fontSize);
        }

        ImGui::PopItemWidth();
        ImGui::TextDisabled("UI scale lives in the bar above -- it affects every page.");

        ImGui::TextDisabled("global %.2fx  (user %.2f x native %.2f)   font dpi %.0f",
                            double(Scale::GetGlobalScale()), double(Scale::GetUserScale()),
                            double(Scale::GetNativeScale()), double(Scale::GetFontDpi()));
    }
    EndSection();

    if (BeginSection("Specimen")) {
        const char* pangram = "Sphinx of black quartz, judge my vow -- 0123456789";
        ImGui::TextUnformatted(pangram);
        ImGui::TextDisabled("%s", pangram);

        ImGui::Spacing();
        ImGui::TextUnformatted("Numerals have to line up in the browsers:");
        if (ImFont* mono = Fonts::GetMonoFont()) {
            ImGui::PushFont(mono);
            ImGui::TextUnformatted("  0x0000DEAD   1,234,567 bytes   00:12:34.567");
            ImGui::TextUnformatted("  0x0000BEEF   7,654,321 bytes   01:02:03.004");
            ImGui::PopFont();
        } else {
            ImGui::TextDisabled("  (bundled mono font unavailable)");
        }

        ImGui::Spacing();
        ImGui::TextUnformatted("Icons inline with text: " ICON_SF_FOLDER " folder   " ICON_SF_DOCUMENT
                               " document   " ICON_SF_CUBE_FILL " mesh");

        ImGui::Spacing();
        const ImGuiIO& io = ImGui::GetIO();
        const ImGuiStyle& st = ImGui::GetStyle();
        ImGui::TextDisabled("base %.1f px x main %.2f x dpi %.2f = drawn %.1f px   "
                            "line height %.1f px   fonts in atlas %d",
                            double(st.FontSizeBase), double(st.FontScaleMain),
                            double(st.FontScaleDpi), double(ImGui::GetFontSize()),
                            double(ImGui::GetTextLineHeight()),
                            io.Fonts ? io.Fonts->Fonts.Size : 0);
    }
    EndSection();
}

// ── Page: Icons ───────────────────────────────────────────────────────────

void UiGallery::DrawIconsPage() {
    const Fonts::IconEntry* icons = Fonts::IconTable();
    const int count = Fonts::IconCount();

    ImGui::Spacing();
    ImGui::PushItemWidth(240.0f);
    if (ImGui::InputTextWithHint("##icon_filter", ICON_SF_MAGNIFYINGGLASS " filter by name",
                                 m_iconFilter, IM_ARRAYSIZE(m_iconFilter)))
        m_iconMatchesValid = false;
    ImGui::SameLine();
    ImGui::SliderFloat("Preview size", &m_iconScale, 1.0f, 8.0f, "%.1fx");
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (Widgets::Button("Glyph debugger"))
        m_showGlyphDebug = true;

    // Case-insensitive substring match on the macro name, so typing "gear"
    // finds ICON_SF_GEAR without spelling the shared prefix.
    if (!m_iconMatchesValid || m_iconMatchesFor != m_iconFilter) {
        char needle[IM_ARRAYSIZE(m_iconFilter)];
        const int needleLen = int(std::strlen(m_iconFilter));
        for (int i = 0; i <= needleLen; ++i)
            needle[i] = char(std::toupper((unsigned char)m_iconFilter[i]));

        m_iconMatches.clear();
        m_iconMatches.reserve(size_t(count));
        for (int i = 0; i < count; ++i) {
            if (needleLen == 0 || std::strstr(icons[i].name, needle))
                m_iconMatches.push_back(i);
        }
        m_iconMatchesFor   = m_iconFilter;
        m_iconMatchesValid = true;
    }

    ImGui::SameLine();
    ImGui::TextDisabled("%d of %d", int(m_iconMatches.size()), count);
    if (m_copiedAt > 0.0 && ImGui::GetTime() - m_copiedAt < 2.0) {
        ImGui::SameLine();
        ImGui::TextDisabled("| copied %s", m_copiedIcon.c_str());
    }

    ImGui::Separator();
    if (ImGui::BeginChild("##icon_grid")) {
        if (m_iconMatches.empty()) {
            ImGui::TextDisabled("No icon matches \"%s\".", m_iconFilter);
        } else {
            const ImGuiStyle& style = ImGui::GetStyle();
            // Preview-only glyph size, independent of the app's UI scale. ImGui
            // 1.92 rasterises on demand, so a larger size re-bakes the glyph at
            // that size rather than magnifying the 14px bitmap -- the icons stay
            // sharp all the way up.
            const float glyphSize = style.FontSizeBase * m_iconScale;
            const float cell   = glyphSize + style.FramePadding.x * 4.0f;
            const float stride = cell + style.ItemSpacing.x;
            const int perRow   = std::max(1, int((ImGui::GetContentRegionAvail().x + style.ItemSpacing.x) / stride));
            const int total    = int(m_iconMatches.size());
            const int rows     = (total + perRow - 1) / perRow;

            // Only the visible rows are laid out -- without this the page emits
            // ~6.7k buttons per frame.
            ImGuiListClipper clipper;
            clipper.Begin(rows, cell + style.ItemSpacing.y);
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    for (int col = 0; col < perRow; ++col) {
                        const int slot = row * perRow + col;
                        if (slot >= total)
                            break;
                        if (col > 0)
                            ImGui::SameLine();

                        const Fonts::IconEntry& icon = icons[m_iconMatches[size_t(slot)]];
                        ImGui::PushID(slot);
                        // IconButton (not Button) so the glyph is centred on
                        // its ink; the font push is what re-rasterises it at the
                        // preview size. Popped before the tooltip so the tooltip
                        // stays at the normal UI size.
                        Widgets::IconButtonOpts opts;
                        opts.size = ImVec2(cell, cell);
                        ImGui::PushFont(nullptr, glyphSize);
                        const bool hit = Widgets::IconButton("##icon", icon.value, opts);
                        ImGui::PopFont();
                        if (hit) {
                            ImGui::SetClipboardText(icon.name);
                            m_copiedIcon = icon.name;
                            m_copiedAt   = ImGui::GetTime();
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("%s\n(click to copy the macro name)", icon.name);
                        ImGui::PopID();
                    }
                }
            }
        }
    }
    ImGui::EndChild();
}

// ── Page: Style ───────────────────────────────────────────────────────────

void UiGallery::DrawStylePage() {
    ImGuiStyle& style = ImGui::GetStyle();

    ImGui::Spacing();
    Hint("Session-only style tweaks. ScaleManager::ApplyStyleScale resets the whole "
         "ImGuiStyle, so moving the UI scale slider discards anything set here.");
    ImGui::Spacing();

    if (BeginSection("Spacing")) {
        ImGui::PushItemWidth(240.0f);
        ImGui::SliderFloat2("WindowPadding", &style.WindowPadding.x, 0.0f, 20.0f, "%.0f");
        ImGui::SliderFloat2("FramePadding", &style.FramePadding.x, 0.0f, 20.0f, "%.0f");
        ImGui::SliderFloat2("ItemSpacing", &style.ItemSpacing.x, 0.0f, 20.0f, "%.0f");
        ImGui::SliderFloat2("CellPadding", &style.CellPadding.x, 0.0f, 20.0f, "%.0f");
        ImGui::SliderFloat("IndentSpacing", &style.IndentSpacing, 0.0f, 30.0f, "%.0f");
        ImGui::SliderFloat("ScrollbarSize", &style.ScrollbarSize, 1.0f, 20.0f, "%.0f");
        ImGui::PopItemWidth();
    }
    EndSection();

    if (BeginSection("Rounding and borders")) {
        ImGui::PushItemWidth(240.0f);
        ImGui::SliderFloat("WindowRounding", &style.WindowRounding, 0.0f, 12.0f, "%.0f");
        ImGui::SliderFloat("FrameRounding", &style.FrameRounding, 0.0f, 12.0f, "%.0f");
        ImGui::SliderFloat("ChildRounding", &style.ChildRounding, 0.0f, 12.0f, "%.0f");
        ImGui::SliderFloat("TabRounding", &style.TabRounding, 0.0f, 12.0f, "%.0f");
        ImGui::SliderFloat("GrabRounding", &style.GrabRounding, 0.0f, 12.0f, "%.0f");
        ImGui::SliderFloat("WindowBorderSize", &style.WindowBorderSize, 0.0f, 2.0f, "%.0f");
        ImGui::SliderFloat("FrameBorderSize", &style.FrameBorderSize, 0.0f, 2.0f, "%.0f");
        ImGui::PopItemWidth();
    }
    EndSection();

    if (BeginSection("Export")) {
        Hint("These are the live (scaled) values. Copy at UI scale 1.0x if you mean "
             "to paste them into Theme::ApplyStyleDefaults, which is authored in "
             "logical units.");
        if (Widgets::Button("Copy as C++")) {
            char buf[1024];
            std::snprintf(buf, sizeof(buf),
                          "ImGuiStyle& s = ImGui::GetStyle();\n"
                          "s.WindowPadding    = ImVec2(%.1ff, %.1ff);\n"
                          "s.FramePadding     = ImVec2(%.1ff, %.1ff);\n"
                          "s.ItemSpacing      = ImVec2(%.1ff, %.1ff);\n"
                          "s.CellPadding      = ImVec2(%.1ff, %.1ff);\n"
                          "s.IndentSpacing    = %.1ff;\n"
                          "s.ScrollbarSize    = %.1ff;\n"
                          "s.WindowRounding   = %.1ff;\n"
                          "s.FrameRounding    = %.1ff;\n"
                          "s.ChildRounding    = %.1ff;\n"
                          "s.TabRounding      = %.1ff;\n"
                          "s.GrabRounding     = %.1ff;\n"
                          "s.WindowBorderSize = %.1ff;\n"
                          "s.FrameBorderSize  = %.1ff;\n",
                          double(style.WindowPadding.x), double(style.WindowPadding.y),
                          double(style.FramePadding.x), double(style.FramePadding.y),
                          double(style.ItemSpacing.x), double(style.ItemSpacing.y),
                          double(style.CellPadding.x), double(style.CellPadding.y),
                          double(style.IndentSpacing), double(style.ScrollbarSize),
                          double(style.WindowRounding), double(style.FrameRounding),
                          double(style.ChildRounding), double(style.TabRounding),
                          double(style.GrabRounding), double(style.WindowBorderSize),
                          double(style.FrameBorderSize));
            ImGui::SetClipboardText(buf);
        }
        ImGui::SameLine();
        if (Widgets::Button("Reset to scaled defaults")) {
            Scale::ApplyStyleScale(Scale::GetUserScale());
            Theme::ApplyTheme(Theme::GetAccent());
        }
    }
    EndSection();
}

// ── Page: Diagnostics ─────────────────────────────────────────────────────

void UiGallery::DrawDiagnosticsPage() {
    const ImGuiIO& io = ImGui::GetIO();

    ImGui::Spacing();
    if (BeginSection("Frame")) {
        const float fps = (io.Framerate > 0.0f) ? io.Framerate : 1.0f;
        ImGui::Text("%.1f FPS   %.3f ms/frame", double(io.Framerate), double(1000.0f / fps));
        ImGui::Text("%d vertices   %d indices   %d windows rendered",
                    io.MetricsRenderVertices, io.MetricsRenderIndices, io.MetricsRenderWindows);
        ImGui::Text("%d active windows", io.MetricsActiveWindows);
        ImGui::Text("display %.0f x %.0f   framebuffer scale %.2f x %.2f",
                    double(io.DisplaySize.x), double(io.DisplaySize.y),
                    double(io.DisplayFramebufferScale.x), double(io.DisplayFramebufferScale.y));
    }
    EndSection();

    if (BeginSection("Dear ImGui tools")) {
        ImGui::Checkbox("Demo window", &m_showDemo);
        ImGui::Checkbox("Metrics / debugger", &m_showMetrics);
        ImGui::Checkbox("Style editor", &m_showStyleEditor);
        ImGui::Checkbox("ID stack tool", &m_showStackTool);
        ImGui::Checkbox("SF Symbols glyph debugger", &m_showGlyphDebug);
        ImGui::Spacing();
        ImGui::TextDisabled("Dear ImGui %s (%d)", IMGUI_VERSION, IMGUI_VERSION_NUM);
    }
    EndSection();
}

} // namespace Onyx::App
