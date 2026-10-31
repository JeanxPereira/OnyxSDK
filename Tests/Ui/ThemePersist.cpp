#include <doctest/doctest.h>

#include "Onyx/Core/Services/Settings.h"
#include "Onyx/Core/Services/SettingsSchema.h"
#include "Onyx/Ui/Theme.h"

using namespace Onyx::Ui;
using Onyx::Services::Settings;

namespace {

float Byte(int v) {
    return static_cast<float>(v) / 255.0f;
}

}

TEST_CASE("Missing keys give the default state") {
    Settings settings = Settings::Empty();
    const Theme theme(settings);
    CHECK(theme.State() == ThemeState{});
}

TEST_CASE("ThemeState survives Settings and back") {
    ThemeState state;
    state.fontPath = "C:/fonts/Some Font.ttf";
    state.fontSizePt = 18.5f;
    state.userScale = 1.25f;
    state.accent = glm::vec4(Byte(10), Byte(200), Byte(77), Byte(255));
    state.mode = ThemeMode::Light;
    state.overrides.push_back({"TabHovered", glm::vec4(Byte(255), Byte(0), Byte(16), Byte(128))});
    state.overrides.push_back({"Text", glm::vec4(Byte(1), Byte(2), Byte(3), Byte(4))});

    Settings settings = Settings::Empty();
    Theme writer(settings);
    writer.Set(state);
    writer.Save();

    const Theme reader(settings);
    CHECK(reader.State() == state);
}

TEST_CASE("ReadState and WriteState round-trip without a Theme") {
    ThemeState state;
    state.fontSizePt = 20.0f;
    state.userScale = 1.5f;
    state.mode = ThemeMode::Dark;
    state.accent = glm::vec4(Byte(40), Byte(80), Byte(120), Byte(255));
    state.overrides.push_back({"Text", glm::vec4(Byte(1), Byte(2), Byte(3), Byte(4))});

    Settings settings = Settings::Empty();
    Theme::WriteState(settings, state);
    CHECK(Theme::ReadState(settings) == state);
    CHECK(Theme(settings).State() == state);
}

TEST_CASE("Each mode round-trips") {
    for (ThemeMode mode : {ThemeMode::Dark, ThemeMode::Light, ThemeMode::System}) {
        Settings settings = Settings::Empty();
        Theme writer(settings);
        writer.Mutate([&](ThemeState& s) { s.mode = mode; });
        writer.Save();
        CHECK(Theme(settings).State().mode == mode);
    }
}

TEST_CASE("Save writes the Settings object and nothing else") {
    Settings settings = Settings::Empty();
    Theme theme(settings);
    theme.Save();
    CHECK(settings.Has("ui.theme.mode"));
    CHECK(settings.Has("ui.theme.fontSizePt"));
    CHECK(settings.Has("ui.theme.userScale"));
    CHECK(settings.Has("ui.theme.fontPath"));
    CHECK(settings.Has("ui.theme.accent"));
    CHECK(settings.Has("ui.theme.overrides"));
    CHECK(settings.Dirty());
}

TEST_CASE("Unknown override tokens and bad values are ignored") {
    Settings settings = Settings::Empty();
    settings.Set("ui.theme.overrides", "Bogus=#ffffffff;TabHovered=#ff0000ff;Text=nothex");
    settings.Set("ui.theme.accent", "not a colour");
    settings.Set("ui.theme.userScale", 99.0);
    const Theme theme(settings);
    REQUIRE(theme.State().overrides.size() == 1);
    CHECK(theme.State().overrides[0].token == "TabHovered");
    CHECK(theme.State().accent == ThemeState{}.accent);
    CHECK(theme.State().userScale == 3.0f);
}

// ui.theme.overrides is intentionally outside the schema: it is a free-form list, not a setting
// with a range or an option set.
TEST_CASE("The schema documents the five scalar theme keys") {
    Onyx::Services::SettingsSchema schema;
    Theme::DescribeSettings(schema);
    CHECK(schema.Entries().size() == 5);

    Settings settings = Settings::Empty();
    Onyx::Services::ApplyDefaults(schema, settings);
    const glm::vec4 accent = Theme(settings).State().accent;
    CHECK(accent.x == doctest::Approx(ThemeState{}.accent.x).epsilon(0.01));
    CHECK(accent.z == doctest::Approx(ThemeState{}.accent.z).epsilon(0.01));
}
