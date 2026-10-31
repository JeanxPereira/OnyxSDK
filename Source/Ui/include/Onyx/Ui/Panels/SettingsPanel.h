#pragma once

#include <Onyx/Core/Services/Settings.h>
#include <Onyx/Core/Services/SettingsSchema.h>
#include <Onyx/Ui/ThemeState.h>
#include <Onyx/Ui/Panel.h>

#include <cstddef>
#include <string>
#include <vector>

namespace Onyx::Ui {

struct SettingsTab {
    std::string id;
    std::string title;
    std::string label;  // title###id, unique even when two modules share a display name
    std::vector<Services::SettingsSchema::Entry> entries;
};

struct SettingsModel {
    // tabs[0] is Appearance; one tab per registered module follows.
    std::vector<SettingsTab> tabs;
};

class SettingsPanel final : public IPanel {
public:
    std::string_view Id() const override { return "onyx.settings"; }
    std::string_view Title() const override { return "Settings"; }
    void Draw(UiContext& ui) override;

    static SettingsModel Model(UiContext& ui);

    // The Appearance values as Theme::Save writes them; the Appearance tab edits this copy of
    // the keys DescribeSettings declares and reads it back with Theme::ReadState. The copy is
    // rebuilt only when the theme's state changed from elsewhere.
    const Services::Settings& AppearanceValues() const { return m_appearance; }

    // Writes the theme and any dirty settings to disk.
    static void Apply(UiContext& ui);
    // Restores the defaults of one tab: the theme for Appearance, the schema defaults for a module.
    static void Reset(UiContext& ui, const SettingsModel& model, size_t tab);

private:
    SettingsModel m_model;
    Services::Settings m_appearance = Services::Settings::Empty();
    ThemeState m_appearanceFor;
    bool m_appearanceBuilt = false;
    size_t m_moduleCount = ~size_t{0};
};

}
