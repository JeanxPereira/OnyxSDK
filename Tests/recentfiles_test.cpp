// ── RecentFiles tests (doctest) ───────────────────────────────────────────
//
// Load() deliberately drops entries whose file has since been deleted, so the
// fixture writes real files into a temp dir instead of using fake paths.

#include <doctest/doctest.h>
#include <Onyx/Services/RecentFiles.h>

#include <filesystem>
#include <fstream>
#include <string>

using namespace Onyx::Services;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path root;

    explicit TempDir(const char* name) : root(fs::temp_directory_path() / name) {
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    std::string touch(const std::string& name) const {
        const fs::path p = root / name;
        std::ofstream(p) << "x";
        return p.string();
    }
};

} // namespace

TEST_CASE("RecentFiles puts the newest entry first and de-duplicates by path") {
    TempDir dir("onyx_recentfiles_order");
    RecentFiles recents;

    const std::string a = dir.touch("a.iso");
    const std::string b = dir.touch("b.iso");

    recents.Add(a, "gow2", "ISO");
    recents.Add(b, "gow1", "ISO");

    REQUIRE(recents.Entries().size() == 2);
    CHECK(recents.Entries()[0].path == b);
    CHECK(recents.Entries()[1].path == a);
    CHECK(recents.Entries()[0].displayName == "b.iso");

    // Re-adding an existing path moves it to the front instead of duplicating,
    // and refreshes its metadata.
    recents.Add(a, "ragnarok", "WAD");
    REQUIRE(recents.Entries().size() == 2);
    CHECK(recents.Entries()[0].path == a);
    CHECK(recents.Entries()[0].gameHint == "ragnarok");
    CHECK(recents.Entries()[0].fileType == "WAD");
}

TEST_CASE("RecentFiles caps the list at MAX_RECENTS") {
    TempDir dir("onyx_recentfiles_cap");
    RecentFiles recents;

    const int extra = 3;
    for (int i = 0; i < RecentFiles::MAX_RECENTS + extra; ++i)
        recents.Add(dir.touch("f" + std::to_string(i) + ".iso"), "gow2", "ISO");

    CHECK(int(recents.Entries().size()) == RecentFiles::MAX_RECENTS);
    // The most recent add survives; the first ones fell off the end.
    CHECK(recents.Entries().front().displayName ==
          "f" + std::to_string(RecentFiles::MAX_RECENTS + extra - 1) + ".iso");
}

TEST_CASE("RecentFiles round-trips through the tab-separated file") {
    TempDir dir("onyx_recentfiles_roundtrip");
    const std::string listPath = (dir.root / "recents.txt").string();
    const std::string kept    = dir.touch("kept.iso");
    const std::string deleted = dir.touch("deleted.iso");

    {
        RecentFiles writer;
        writer.Add(kept, "gow2", "ISO");
        writer.Add(deleted, "gow1", "WAD");
        writer.Save(listPath);
    }

    SUBCASE("entries survive a save/load cycle") {
        RecentFiles reader;
        reader.Load(listPath);
        REQUIRE(reader.Entries().size() == 2);
        CHECK(reader.Entries()[0].path == deleted);
        CHECK(reader.Entries()[0].gameHint == "gow1");
        CHECK(reader.Entries()[0].fileType == "WAD");
        CHECK(reader.Entries()[1].path == kept);
    }

    SUBCASE("entries whose file vanished are dropped on load") {
        fs::remove(deleted);
        RecentFiles reader;
        reader.Load(listPath);
        REQUIRE(reader.Entries().size() == 1);
        CHECK(reader.Entries()[0].path == kept);
    }

    SUBCASE("loading a missing list leaves an empty, usable object") {
        RecentFiles reader;
        reader.Add(kept, "gow2", "ISO");
        reader.Load((dir.root / "does_not_exist.txt").string());
        CHECK(reader.Empty());
    }
}

TEST_CASE("RecentFiles auto-saves once a load path is known") {
    TempDir dir("onyx_recentfiles_autosave");
    const std::string listPath = (dir.root / "recents.txt").string();
    const std::string file     = dir.touch("auto.iso");

    RecentFiles recents;
    recents.Load(listPath);   // path does not exist yet, but is remembered
    recents.Add(file, "gow2", "ISO");

    REQUIRE(fs::exists(listPath));
    RecentFiles reloaded;
    reloaded.Load(listPath);
    REQUIRE(reloaded.Entries().size() == 1);
    CHECK(reloaded.Entries()[0].path == file);

    recents.Clear();
    CHECK(recents.Empty());
}
