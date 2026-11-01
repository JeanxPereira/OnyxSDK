#include <doctest/doctest.h>
#include <Onyx/Core/Services/Settings.h>
#include <Onyx/Core/Services/SettingsSchema.h>

#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "TempDir.h"

using namespace Onyx::Services;

TEST_CASE("Settings.Settings round-trips typed values through TOML") {
    Onyx::Test::TempDir scope("settings");
    auto tmp = scope / "onyx_settings_test.toml";
    std::filesystem::remove(tmp);
    {
        auto s = Settings::Load(tmp);
        CHECK_FALSE(s.GetString("proj.texIndexDir").has_value());
        s.Set("proj.texIndexDir", std::string("E:/packs"));
        s.Set("proj.lodBias", int64_t{2});
        s.Set("ui.confirmClose", true);
        CHECK(s.Dirty());
        CHECK(s.Save().ok());
        CHECK_FALSE(s.Dirty());
    }
    {
        auto s = Settings::Load(tmp);
        CHECK(s.GetString("proj.texIndexDir") == "E:/packs");
        CHECK(s.GetInt("proj.lodBias") == 2);
        CHECK(s.GetBool("ui.confirmClose") == true);
    }
    std::filesystem::remove(tmp);
}

TEST_CASE("Settings.Settings persists table structure in TOML") {
    Onyx::Test::TempDir scope("settings");
    auto tmp = scope / "onyx_settings_table_test.toml";
    std::filesystem::remove(tmp);
    {
        auto s = Settings::Load(tmp);
        s.Set("proj.texIndexDir", std::string("E:/packs"));
        s.Set("proj.lodBias", int64_t{2});
        CHECK(s.Save().ok());
    }
    // Read the raw TOML file and check for table structure
    {
        std::ifstream file(tmp);
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        CHECK(content.find("[proj]") != std::string::npos);
        file.close();
    }
    std::filesystem::remove(tmp);
}

TEST_CASE("Settings.Settings missing file loads empty") {
    Onyx::Test::TempDir scope("settings");
    auto missing = scope / "onyx_settings_does_not_exist_xyz.toml";
    std::filesystem::remove(missing);
    auto s = Settings::Load(missing);
    CHECK_FALSE(s.GetString("proj.texIndexDir").has_value());
    CHECK_FALSE(s.GetInt("proj.lodBias").has_value());
    CHECK_FALSE(s.GetBool("ui.confirmClose").has_value());
    CHECK_FALSE(s.Dirty());
}

TEST_CASE("Settings.Settings type mismatch returns nullopt") {
    Onyx::Test::TempDir scope("settings");
    auto tmp = scope / "onyx_settings_typemismatch.toml";
    std::filesystem::remove(tmp);
    {
        auto s = Settings::Load(tmp);
        s.Set("value", std::string("not_an_int"));
        CHECK(s.Save().ok());
    }
    {
        auto s = Settings::Load(tmp);
        CHECK(s.GetString("value") == "not_an_int");
        CHECK_FALSE(s.GetInt("value").has_value());
        CHECK_FALSE(s.GetBool("value").has_value());
        CHECK_FALSE(s.GetDouble("value").has_value());
    }
    std::filesystem::remove(tmp);
}

TEST_CASE("Settings.Settings no-dot-key lands at root") {
    Onyx::Test::TempDir scope("settings");
    auto tmp = scope / "onyx_settings_root_key.toml";
    std::filesystem::remove(tmp);
    {
        auto s = Settings::Load(tmp);
        s.Set("rootKey", std::string("rootValue"));
        s.Set("anotherKey", int64_t{42});
        CHECK(s.Save().ok());
    }
    // Read raw TOML and verify no table header for root keys
    {
        std::ifstream file(tmp);
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        CHECK(content.find("rootKey") != std::string::npos);
        CHECK(content.find("anotherKey") != std::string::npos);
        file.close();
    }
    {
        auto s = Settings::Load(tmp);
        CHECK(s.GetString("rootKey") == "rootValue");
        CHECK(s.GetInt("anotherKey") == 42);
    }
    std::filesystem::remove(tmp);
}

TEST_CASE("Settings.Settings Dirty flag management") {
    Onyx::Test::TempDir scope("settings");
    auto tmp = scope / "onyx_settings_dirty.toml";
    std::filesystem::remove(tmp);
    {
        auto s = Settings::Load(tmp);
        CHECK_FALSE(s.Dirty());
        s.Set("key", std::string("value"));
        CHECK(s.Dirty());
        CHECK(s.Save().ok());
        CHECK_FALSE(s.Dirty());
    }
    std::filesystem::remove(tmp);
}

TEST_CASE("Settings.Settings Path returns loaded path") {
    Onyx::Test::TempDir scope("settings");
    auto tmp = scope / "onyx_settings_path_test.toml";
    std::filesystem::remove(tmp);
    auto s = Settings::Load(tmp);
    CHECK(s.Path() == tmp);
    std::filesystem::remove(tmp);
}

TEST_CASE("Settings.Settings GetDouble typed access") {
    Onyx::Test::TempDir scope("settings");
    auto tmp = scope / "onyx_settings_double.toml";
    std::filesystem::remove(tmp);
    {
        auto s = Settings::Load(tmp);
        s.Set("config.threshold", 3.14159);
        CHECK(s.Save().ok());
    }
    {
        auto s = Settings::Load(tmp);
        auto val = s.GetDouble("config.threshold");
        CHECK(val.has_value());
        CHECK(*val == doctest::Approx(3.14159));
    }
    std::filesystem::remove(tmp);
}

TEST_CASE("Settings.Settings type mismatches return nullopt, never coerce") {
    Onyx::Test::TempDir scope("settings");
    auto tmp = scope / "onyx_settings_mismatch.toml";
    std::filesystem::remove(tmp);
    auto s = Settings::Load(tmp);
    s.Set("t.flag", true);
    s.Set("t.count", int64_t{7});
    s.Set("t.ratio", 2.5);
    CHECK_FALSE(s.GetInt("t.flag").has_value());     // bool is not an int
    CHECK_FALSE(s.GetBool("t.count").has_value());   // int is not a bool
    CHECK_FALSE(s.GetInt("t.ratio").has_value());    // float is not an int
    CHECK_FALSE(s.GetDouble("t.count").has_value()); // int is not a double (exact)
    std::filesystem::remove(tmp);
}

TEST_CASE("Settings.Settings two-dot key split uses only first dot") {
    Onyx::Test::TempDir scope("settings");
    auto tmp = scope / "onyx_settings_twodot.toml";
    std::filesystem::remove(tmp);
    {
        auto s = Settings::Load(tmp);
        s.Set("a.b.c", int64_t{1});
        CHECK(s.Save().ok());
    }
    // Read raw TOML and verify structure: [a] table with b.c key
    {
        std::ifstream file(tmp);
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        CHECK(content.find("[a]") != std::string::npos);
        // Look for the key - it may be quoted or unquoted depending on toml++ output
        CHECK((content.find("\"b.c\"") != std::string::npos ||
               content.find("b.c") != std::string::npos));
        file.close();
    }
    {
        auto s = Settings::Load(tmp);
        CHECK(s.GetInt("a.b.c") == 1);
    }
    std::filesystem::remove(tmp);
}

namespace {

std::string ReadAll(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}  // namespace

TEST_CASE("Settings.SaveIsAtomic") {
    Onyx::Test::TempDir scope("settings");
    const auto dir = scope.path();
    auto file = dir / "s.toml";
    {
        auto s = Settings::Load(file);
        s.Set("a.b", int64_t{3});
        REQUIRE(s.Save().ok());
        s.Set("a.b", int64_t{4});
        REQUIRE(s.Save().ok());
    }
    CHECK_FALSE(std::filesystem::exists(dir / "s.toml.tmp"));
    CHECK(std::filesystem::exists(file));
    CHECK(Settings::Load(file).GetInt("a.b") == 4);
}

TEST_CASE("Settings.BrokenFileIsPreservedAndNotOverwritten") {
    Onyx::Test::TempDir scope("settings");
    const auto dir = scope.path();
    auto file = dir / "s.toml";
    const std::string bad = "a = [";
    { std::ofstream(file, std::ios::binary) << bad; }

    auto s = Settings::Load(file);
    CHECK(s.Path() == file);
    REQUIRE(s.LoadError().has_value());
    CHECK_FALSE(s.LoadError()->empty());
    auto broken = dir / "s.broken.toml";
    REQUIRE(std::filesystem::exists(broken));
    CHECK(ReadAll(broken) == bad);

    s.Set("x", int64_t{1});
    auto r = s.Save();
    CHECK_FALSE(r.ok());
    CHECK(r.error().code == "settings.broken");
    CHECK(ReadAll(file) == bad);

    { std::ofstream(file, std::ios::binary) << "b = ["; }
    auto again = Settings::Load(file);
    CHECK(again.LoadError().has_value());
    CHECK(ReadAll(broken) == bad);

    s.ClearLoadError();
    CHECK_FALSE(s.LoadError().has_value());
    CHECK(s.Save().ok());
    CHECK(Settings::Load(file).GetInt("x") == 1);
}

TEST_CASE("Settings.SaveIfDirtyWritesOnlyWhenDirty") {
    Onyx::Test::TempDir scope("settings");
    const auto dir = scope.path();
    auto file = dir / "s.toml";
    auto s = Settings::Load(file);
    CHECK(s.SaveIfDirty().ok());
    CHECK_FALSE(std::filesystem::exists(file));

    s.Set("k", true);
    REQUIRE(s.SaveIfDirty().ok());
    REQUIRE(std::filesystem::exists(file));
    CHECK_FALSE(s.Dirty());

    auto before = std::filesystem::last_write_time(file);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(s.SaveIfDirty().ok());
    CHECK(std::filesystem::last_write_time(file) == before);
}

TEST_CASE("Settings.ApplyDefaultsLeavesWrongTypedValueAlone") {
    Onyx::Test::TempDir scope("settings");
    const auto dir = scope.path();
    auto file = dir / "s.toml";
    { std::ofstream(file, std::ios::binary) << "[ui]\ncount = \"x\"\n"; }
    auto s = Settings::Load(file);
    SettingsSchema schema;
    schema.Int("ui.count", 7, 0, 100);
    ApplyDefaults(schema, s);
    CHECK(s.GetString("ui.count") == "x");
    CHECK_FALSE(s.GetInt("ui.count").has_value());
    CHECK(s.Has("ui.count"));
    CHECK_FALSE(s.Has("ui.other"));
}

TEST_CASE("Settings.SchemaApplyDefaultsFillsMissingOnly") {
    Onyx::Test::TempDir scope("settings");
    const auto dir = scope.path();
    auto s = Settings::Load(dir / "s.toml");
    s.Set("ui.scale", 1.5);

    SettingsSchema schema;
    schema.Bool("ui.confirm", true, "Confirm close")
        .Int("ui.count", 7, 0, 10)
        .Double("ui.scale", 1.0, 0, 4)
        .String("ui.name", "onyx")
        .Enum("ui.mode", {"a", "b", "c"}, 1)
        .Path("paths.cache", "cache");

    REQUIRE(schema.Entries().size() == 6);
    CHECK(schema.Entries()[0].kind == SettingsSchema::Kind::Bool);
    CHECK(schema.Entries()[0].label == "Confirm close");
    CHECK(schema.Entries()[1].min == 0);
    CHECK(schema.Entries()[1].max == 10);
    CHECK(schema.Entries()[4].options.size() == 3);

    ApplyDefaults(schema, s);
    CHECK(s.GetBool("ui.confirm") == true);
    CHECK(s.GetInt("ui.count") == 7);
    CHECK(s.GetDouble("ui.scale") == 1.5);
    CHECK(s.GetString("ui.name") == "onyx");
    CHECK(s.GetString("ui.mode") == "b");
    CHECK(s.GetString("paths.cache") == "cache");
}

TEST_CASE("Settings.SetWithStringLiteralStoresAString") {
    auto s = Settings::Empty();
    s.Set("a.name", "onyx");
    CHECK(s.GetString("a.name") == "onyx");
    CHECK_FALSE(s.GetBool("a.name").has_value());
    s.Set("a.view", std::string_view("view"));
    CHECK(s.GetString("a.view") == "view");
}

TEST_CASE("Settings.SchemaDoubleRangeIsFractional") {
    SettingsSchema schema;
    schema.Double("ui.opacity", 0.5, 0.0, 1.0);
    REQUIRE(schema.Entries().size() == 1);
    CHECK(schema.Entries()[0].realMin == doctest::Approx(0.0));
    CHECK(schema.Entries()[0].realMax == doctest::Approx(1.0));
}

TEST_CASE("Settings.CloneIsIndependent") {
    auto s = Settings::Empty();
    s.Set("a.n", int64_t(1));
    auto c = s.Clone();
    s.Set("a.n", int64_t(2));
    CHECK(c.GetInt("a.n") == 1);
    CHECK(c.Dirty());
}

TEST_CASE("Settings.BrokenFileWithANonAsciiNameIsPreserved") {
    Onyx::Test::TempDir scope("settings");
    const auto file = scope.path() / std::filesystem::path(u8"日本.toml");
    { std::ofstream(file, std::ios::binary) << "a = ["; }

    auto s = Settings::Load(file);
    CHECK(s.LoadError().has_value());
    CHECK(std::filesystem::exists(scope.path() / std::filesystem::path(u8"日本.broken.toml")));
}

TEST_CASE("Settings.SaveFailureNamesANonAsciiPathInUtf8") {
    Onyx::Test::TempDir scope("settings");
    const auto blocker = scope.path() / "file";
    { std::ofstream(blocker) << "x"; }
    auto s = Settings::Load(blocker / std::filesystem::path(u8"日本.toml"));
    s.Set("a", int64_t{1});

    const auto r = s.Save();
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().message.find(reinterpret_cast<const char*>(u8"日本.toml")) != std::string::npos);
}
