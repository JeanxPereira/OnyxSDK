#include <doctest/doctest.h>

#include <Onyx/Ui/Panels/SettingsPanel.h>

#include <algorithm>

#include "HeadlessImGui.h"

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

TEST_CASE("SettingsPanel renders every Appearance entry DescribeSettings declares") {
    ObxUi fx("settings_appearance");
    UiContext ui = fx.Make();

    Services::SettingsSchema declared;
    Theme::DescribeSettings(declared);
    const SettingsModel model = SettingsPanel::Model(ui);
    REQUIRE(model.tabs[0].entries.size() == declared.Entries().size());
    for (size_t i = 0; i < declared.Entries().size(); ++i) {
        CHECK(model.tabs[0].entries[i].key == declared.Entries()[i].key);
    }

    SettingsPanel panel;
    HeadlessImGui rig;
    DrawFrames(rig, 3, [&] { panel.Draw(ui); });
    for (const auto& entry : declared.Entries()) {
        CHECK_MESSAGE(panel.AppearanceValues().Has(entry.key), entry.key);
    }
}

TEST_CASE("SettingsPanel mirrors an outside theme change into its Appearance copy") {
    ObxUi fx("settings_mirror");
    UiContext ui = fx.Make();
    // The accent is stored as #rrggbb; one that is already byte-exact survives the copy unchanged.
    fx.theme.Mutate([](ThemeState& state) { state.accent = glm::vec4(51 / 255.0f, 102 / 255.0f, 153 / 255.0f, 1.0f); });
    SettingsPanel panel;
    HeadlessImGui rig;
    DrawFrames(rig, 2, [&] { panel.Draw(ui); });
    CHECK(Theme::ReadState(panel.AppearanceValues()) == fx.theme.State());

    fx.theme.Mutate([](ThemeState& state) { state.userScale = 2.0f; });
    DrawFrames(rig, 2, [&] { panel.Draw(ui); });
    CHECK(Theme::ReadState(panel.AppearanceValues()).userScale == doctest::Approx(2.0f));
    CHECK(Theme::ReadState(panel.AppearanceValues()) == fx.theme.State());
}

TEST_CASE("SettingsPanel tab labels stay unique when display names collide") {
    ObxUi fx("settings_labels");
    UiContext ui = fx.Make();
    const SettingsModel model = SettingsPanel::Model(ui);
    for (const SettingsTab& tab : model.tabs) {
        CHECK(tab.label == tab.title + "###" + tab.id);
    }
}
