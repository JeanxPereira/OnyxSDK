#pragma once

#include <Onyx/Ui/Panel.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Onyx::Ui {

// A reference of what the Ui layer draws: every widget in its states, the resolved palette with
// contrast ratios, the fonts, the icon table and the applied style. Hidden until a host shows it.
// It reads the theme and never writes ImGuiStyle; nothing it changes is persisted.
class Gallery final : public IPanel {
public:
    static constexpr size_t kPageCount = 5;

    Gallery() { visible = false; }

    std::string_view Id() const override { return "onyx.gallery"; }
    std::string_view Title() const override { return "Gallery"; }
    void Draw(UiContext& ui) override;

    // Draws one page's body without the tab bar: 0 Widgets, 1 Theme, 2 Typography, 3 Icons, 4 Style.
    void DrawPage(size_t index, UiContext& ui);

    // Indices into IconTable() whose define name contains `filter`, ignoring case. The list is
    // rebuilt only when the filter text differs from the previous call.
    const std::vector<uint32_t>& IconMatches(std::string_view filter);

private:
    void DrawWidgets();
    void DrawTheme(UiContext& ui);
    void DrawTypography(UiContext& ui);
    void DrawIcons();
    void DrawStyle(UiContext& ui);

    std::vector<uint32_t> m_iconMatches;
    std::string m_iconMatchesFor;
    bool m_iconMatchesBuilt = false;
    std::array<char, 64> m_iconFilter{};
    float m_iconScale = 1.0f;
    std::string m_copied;

    bool m_toggle = false;
    bool m_check = true;
    int m_radio = 0;
    int m_combo = 0;
    float m_slider = 0.5f;
    float m_progress = 0.0f;
    std::array<char, 64> m_text{};
};

}
