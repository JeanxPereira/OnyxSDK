#pragma once

#include <Onyx/Core/Services/SettingsSchema.h>
#include <Onyx/Ui/Panel.h>

#include <cstddef>
#include <string>
#include <vector>

namespace Onyx::Ui {

struct SettingsTab {
    std::string id;
    std::string title;
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

    // Writes the theme and any dirty settings to disk.
    static void Apply(UiContext& ui);
    // Restores the defaults of one tab: the theme for Appearance, the schema defaults for a module.
    static void Reset(UiContext& ui, const SettingsModel& model, size_t tab);

private:
    SettingsModel m_model;
    size_t m_moduleCount = ~size_t{0};
};

}
