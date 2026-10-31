#include <doctest/doctest.h>

#include <Onyx/Ui/Panels/SettingsPanel.h>

#include <algorithm>

#include "Support/ObxWorkspace.h"

using namespace Onyx;
using namespace Onyx::Ui;

namespace {

const Services::SettingsSchema::Entry* FindEntry(const SettingsTab& tab, const std::string& key) {
    const auto it = std::find_if(tab.entries.begin(), tab.entries.end(),
                                 [&](const Services::SettingsSchema::Entry& e) { return e.key == key; });
    return it == tab.entries.end() ? nullptr : &*it;
}

}

TEST_CASE("SettingsPanel lists Appearance and the obx module schema") {
    ObxUi fx("settings_model");
    UiContext ui = fx.Make();

    const SettingsModel model = SettingsPanel::Model(ui);
    REQUIRE(model.tabs.size() == 2);
    CHECK(model.tabs[0].title == "Appearance");
    CHECK(FindEntry(model.tabs[0], "ui.theme.mode") != nullptr);
    CHECK(FindEntry(model.tabs[0], "ui.theme.userScale") != nullptr);

    CHECK(model.tabs[1].id == "obx");
    const auto* strict = FindEntry(model.tabs[1], "obx.strictToc");
    REQUIRE(strict != nullptr);
    CHECK(strict->kind == Services::SettingsSchema::Kind::Bool);
}

TEST_CASE("SettingsPanel resets a module tab to its defaults and the theme to its state defaults") {
    ObxUi fx("settings_reset");
    UiContext ui = fx.Make();
    const SettingsModel model = SettingsPanel::Model(ui);

    fx.obx.workspace.Settings().Set("obx.strictToc", true);
    SettingsPanel::Reset(ui, model, 1);
    CHECK(fx.obx.workspace.Settings().GetBool("obx.strictToc") == false);

    fx.theme.Mutate([](ThemeState& s) { s.userScale = 2.0f; });
    SettingsPanel::Reset(ui, model, 0);
    CHECK(fx.theme.State().userScale == doctest::Approx(1.0f));
    SettingsPanel::Reset(ui, model, 99);
}
