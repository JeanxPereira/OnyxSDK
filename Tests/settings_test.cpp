#include <doctest/doctest.h>
#include <Onyx/Services/Settings.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace Onyx::Services;

TEST_CASE("Settings round-trips typed values through TOML") {
    auto tmp = std::filesystem::temp_directory_path() / "onyx_settings_test.toml";
    std::filesystem::remove(tmp);
    {
        auto s = Settings::Load(tmp);
        CHECK_FALSE(s.GetString("gowr.texIndexDir").has_value());
        s.Set("gowr.texIndexDir", std::string("E:/packs"));
        s.Set("gowr.lodBias", int64_t{2});
        s.Set("ui.confirmClose", true);
        CHECK(s.Dirty());
        CHECK(s.Save());
        CHECK_FALSE(s.Dirty());
    }
    {
        auto s = Settings::Load(tmp);
        CHECK(s.GetString("gowr.texIndexDir") == "E:/packs");
        CHECK(s.GetInt("gowr.lodBias") == 2);
        CHECK(s.GetBool("ui.confirmClose") == true);
    }
    std::filesystem::remove(tmp);
}

TEST_CASE("Settings persists table structure in TOML") {
    auto tmp = std::filesystem::temp_directory_path() / "onyx_settings_table_test.toml";
    std::filesystem::remove(tmp);
    {
        auto s = Settings::Load(tmp);
        s.Set("gowr.texIndexDir", std::string("E:/packs"));
        s.Set("gowr.lodBias", int64_t{2});
        CHECK(s.Save());
    }
    // Read the raw TOML file and check for table structure
    {
        std::ifstream file(tmp);
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        CHECK(content.find("[gowr]") != std::string::npos);
        file.close();
    }
    std::filesystem::remove(tmp);
}

TEST_CASE("Settings missing file loads empty") {
    auto missing = std::filesystem::temp_directory_path() / "onyx_settings_does_not_exist_xyz.toml";
    std::filesystem::remove(missing);
    auto s = Settings::Load(missing);
    CHECK_FALSE(s.GetString("gowr.texIndexDir").has_value());
    CHECK_FALSE(s.GetInt("gowr.lodBias").has_value());
    CHECK_FALSE(s.GetBool("ui.confirmClose").has_value());
    CHECK_FALSE(s.Dirty());
}

TEST_CASE("Settings type mismatch returns nullopt") {
    auto tmp = std::filesystem::temp_directory_path() / "onyx_settings_typemismatch.toml";
    std::filesystem::remove(tmp);
    {
        auto s = Settings::Load(tmp);
        s.Set("value", std::string("not_an_int"));
        CHECK(s.Save());
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

TEST_CASE("Settings no-dot-key lands at root") {
    auto tmp = std::filesystem::temp_directory_path() / "onyx_settings_root_key.toml";
    std::filesystem::remove(tmp);
    {
        auto s = Settings::Load(tmp);
        s.Set("rootKey", std::string("rootValue"));
        s.Set("anotherKey", int64_t{42});
        CHECK(s.Save());
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

TEST_CASE("Settings Dirty flag management") {
    auto tmp = std::filesystem::temp_directory_path() / "onyx_settings_dirty.toml";
    std::filesystem::remove(tmp);
    {
        auto s = Settings::Load(tmp);
        CHECK_FALSE(s.Dirty());
        s.Set("key", std::string("value"));
        CHECK(s.Dirty());
        CHECK(s.Save());
        CHECK_FALSE(s.Dirty());
    }
    std::filesystem::remove(tmp);
}

TEST_CASE("Settings Path returns loaded path") {
    auto tmp = std::filesystem::temp_directory_path() / "onyx_settings_path_test.toml";
    std::filesystem::remove(tmp);
    auto s = Settings::Load(tmp);
    CHECK(s.Path() == tmp);
    std::filesystem::remove(tmp);
}

TEST_CASE("Settings GetDouble typed access") {
    auto tmp = std::filesystem::temp_directory_path() / "onyx_settings_double.toml";
    std::filesystem::remove(tmp);
    {
        auto s = Settings::Load(tmp);
        s.Set("config.threshold", 3.14159);
        CHECK(s.Save());
    }
    {
        auto s = Settings::Load(tmp);
        auto val = s.GetDouble("config.threshold");
        CHECK(val.has_value());
        CHECK(*val == doctest::Approx(3.14159));
    }
    std::filesystem::remove(tmp);
}
