#include <doctest/doctest.h>

#include "Onyx/Core/Services/Settings.h"
#include "Onyx/Shell/Recents.h"
#include "Support/ShellSupport.h"

using namespace Onyx;
using Onyx::TestSupport::TempDir;

TEST_CASE("Recents adds newest first and dedups by path") {
    auto settings = Services::Settings::Empty();
    Shell::Recents recents(settings);
    recents.Add("a/one.obx", "obx");
    recents.Add("a/two.obx", "obx");
    recents.Add("a/one.obx", "obx");
    REQUIRE(recents.Entries().size() == 2);
    CHECK(recents.Entries()[0].path == std::filesystem::path("a/one.obx"));
    CHECK(recents.Entries()[0].displayName == "one.obx");
    CHECK(recents.Entries()[0].moduleId == "obx");
    CHECK(recents.Entries()[1].path == std::filesystem::path("a/two.obx"));
}

TEST_CASE("Recents keeps at most kMaxRecents entries") {
    auto settings = Services::Settings::Empty();
    Shell::Recents recents(settings);
    for (int i = 0; i < 15; ++i) {
        recents.Add("dir/file" + std::to_string(i) + ".obx", "obx");
    }
    REQUIRE(recents.Entries().size() == Shell::kMaxRecents);
    CHECK(recents.Entries().front().displayName == "file14.obx");
    CHECK(recents.Entries().back().displayName == "file5.obx");
}

TEST_CASE("Recents persists through Settings and survives separators in a path") {
    TempDir dir("recents");
    const auto file = dir.path / "settings.toml";
    {
        auto settings = Services::Settings::Load(file);
        Shell::Recents recents(settings);
        recents.Add("odd;name|with%chars.obx", "obx");
        recents.Add("plain.obx", "other");
        REQUIRE(settings.Dirty());
        REQUIRE(settings.SaveIfDirty().ok());
    }
    auto settings = Services::Settings::Load(file);
    Shell::Recents recents(settings);
    REQUIRE(recents.Entries().size() == 2);
    CHECK(recents.Entries()[0].path == std::filesystem::path("plain.obx"));
    CHECK(recents.Entries()[0].moduleId == "other");
    CHECK(recents.Entries()[1].path == std::filesystem::path("odd;name|with%chars.obx"));

    recents.Clear();
    CHECK(recents.Entries().empty());
    CHECK(settings.Dirty());
}
