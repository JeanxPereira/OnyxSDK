#include <doctest/doctest.h>

#include "TempDir.h"

#include <filesystem>
#include <fstream>

using Onyx::Test::TempDir;

TEST_CASE("TempDir.TwoInOneProcessDiffer") {
    TempDir a("x");
    TempDir b("x");
    CHECK(a.path() != b.path());
}

TEST_CASE("TempDir.ExistsWhileAliveAndIsGoneAfter") {
    std::filesystem::path where;
    {
        TempDir dir("life");
        where = dir.path();
        CHECK(std::filesystem::is_directory(where));
        std::ofstream(dir / "f.bin") << "data";
        CHECK(std::filesystem::exists(where / "f.bin"));
    }
    CHECK_FALSE(std::filesystem::exists(where));
}

TEST_CASE("TempDir.SlashJoinsUnderThePath") {
    TempDir dir("join");
    CHECK((dir / "a.txt") == dir.path() / "a.txt");
    CHECK((dir / "a.txt").parent_path() == dir.path());
}
