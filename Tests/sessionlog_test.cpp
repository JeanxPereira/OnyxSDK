#include <doctest/doctest.h>

#include <Onyx/Services/Logger.h>
#include <Onyx/Services/SessionLog.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace SL = Onyx::Services::SessionLog;
namespace L  = Onyx::Services::Log;

namespace {

std::tm MakeTm(int year, int mon, int day, int hour, int min, int sec) {
    std::tm t{};
    t.tm_year = year - 1900;
    t.tm_mon  = mon - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = min;
    t.tm_sec  = sec;
    return t;
}

// Each test gets its own scratch directory, wiped on the way in and out so a
// failing run never poisons the next one.
struct ScratchDir {
    std::filesystem::path path;

    explicit ScratchDir(const char* name)
        : path(std::filesystem::temp_directory_path() / "onyx_sessionlog_test" / name) {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    ~ScratchDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

void Touch(const std::filesystem::path& file) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file);
    out << "x\n";
}

std::vector<std::string> FileNamesIn(const std::filesystem::path& dir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (auto& entry : std::filesystem::directory_iterator(dir, ec))
        names.push_back(entry.path().filename().string());
    std::sort(names.begin(), names.end());
    return names;
}

std::string ReadWholeFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Restores the global logging state around a test so session sinks never
// bleed into the rest of the suite.
struct IsolatedLog {
    L::Level previousMin;
    L::Level previousMemoryMin;

    IsolatedLog()
        : previousMin(L::GetMinLevel()),
          previousMemoryMin(L::GetMemoryMinLevel()) {
        L::ClearSinks();
        L::SetMinLevel(L::Level::Trace);
    }
    ~IsolatedLog() {
        L::ClearSinks();
        L::SetMinLevel(previousMin);
        L::SetMemoryMinLevel(previousMemoryMin);
    }
};

} // anonymous namespace

TEST_CASE("[SessionLog] MakeFileName encodes the date and time in a sortable name") {
    std::tm when = MakeTm(2026, 8, 18, 8, 21, 33);
    CHECK(SL::MakeFileName(when) == "onyx-2026-08-18_08-21-33.log");

    // Zero padding everywhere, so lexicographic order is chronological order.
    CHECK(SL::MakeFileName(MakeTm(2026, 1, 2, 3, 4, 5)) == "onyx-2026-01-02_03-04-05.log");

    // Windows forbids ':' in a path -- the name must never grow one.
    CHECK(SL::MakeFileName(when).find(':') == std::string::npos);
}

TEST_CASE("[SessionLog] FormatLine prefixes the canonical shape with a timestamp") {
    std::tm when = MakeTm(2026, 8, 18, 8, 21, 33);

    CHECK(SL::FormatLine(when, 412, L::Level::Info, "wad", "loaded 42 entries")
          == "08:21:33.412 [INFO][wad] loaded 42 entries");

    // Milliseconds are zero padded to three digits.
    CHECK(SL::FormatLine(when, 7, L::Level::Error, "", "boom")
          == "08:21:33.007 [ERROR][] boom");
}

TEST_CASE("[SessionLog] Prune keeps the newest session files and deletes the rest") {
    ScratchDir dir("prune_keeps_newest");

    Touch(dir.path / "onyx-2026-08-15_10-00-00.log");
    Touch(dir.path / "onyx-2026-08-16_10-00-00.log");
    Touch(dir.path / "onyx-2026-08-17_10-00-00.log");
    Touch(dir.path / "onyx-2026-08-18_08-00-00.log");

    CHECK(SL::Prune(dir.path, 2) == 2);

    std::vector<std::string> left = FileNamesIn(dir.path);
    REQUIRE(left.size() == 2);
    CHECK(left[0] == "onyx-2026-08-17_10-00-00.log");
    CHECK(left[1] == "onyx-2026-08-18_08-00-00.log");
}

TEST_CASE("[SessionLog] Prune leaves files it does not own alone") {
    ScratchDir dir("prune_ignores_strangers");

    Touch(dir.path / "onyx-2026-08-15_10-00-00.log");
    Touch(dir.path / "onyx-2026-08-18_08-00-00.log");
    Touch(dir.path / "notes.txt");
    Touch(dir.path / "crash-2026-08-15.log");
    Touch(dir.path / "onyx.toml");

    CHECK(SL::Prune(dir.path, 1) == 1);

    std::vector<std::string> left = FileNamesIn(dir.path);
    REQUIRE(left.size() == 4);
    CHECK(left[0] == "crash-2026-08-15.log");
    CHECK(left[1] == "notes.txt");
    CHECK(left[2] == "onyx-2026-08-18_08-00-00.log");
    CHECK(left[3] == "onyx.toml");
}

TEST_CASE("[SessionLog] Prune on a missing directory is a no-op, not an error") {
    ScratchDir dir("prune_missing");
    CHECK(SL::Prune(dir.path, 3) == 0);
}

TEST_CASE("[SessionLog] Install creates the directory and opens a dated file") {
    IsolatedLog iso;
    ScratchDir dir("install_creates_dir");

    SL::Session session = SL::Install(dir.path);
    REQUIRE(session.ok());

    CHECK(std::filesystem::exists(session.path));
    CHECK(std::filesystem::path(session.path).parent_path() == dir.path);

    // The name carries the session's own date and time.
    std::string name = std::filesystem::path(session.path).filename().string();
    CHECK(name.rfind("onyx-", 0) == 0);
    CHECK(name.size() == std::string("onyx-2026-08-18_08-21-33.log").size());

    SL::Uninstall(session);
}

TEST_CASE("[SessionLog] Install writes records through, flushed line by line") {
    IsolatedLog iso;
    ScratchDir dir("install_writes");

    SL::Session session = SL::Install(dir.path);
    REQUIRE(session.ok());

    ONYX_LOG_INFO("wad", "loaded {} entries", 42);

    // Read while the sink is still installed: a crash must not cost the log.
    std::string contents = ReadWholeFile(session.path);
    CHECK(contents.find("[INFO][wad] loaded 42 entries") != std::string::npos);

    SL::Uninstall(session);
}

TEST_CASE("[SessionLog] Install opens the file with a session header") {
    IsolatedLog iso;
    ScratchDir dir("install_header");

    SL::Session session = SL::Install(dir.path);
    REQUIRE(session.ok());
    SL::Uninstall(session);

    std::string contents = ReadWholeFile(session.path);
    CHECK(contents.find("=== Onyx session ") != std::string::npos);
}

TEST_CASE("[SessionLog] The session file keeps a finer level than the screen") {
    IsolatedLog iso;
    ScratchDir dir("install_level");

    SL::Session session = SL::Install(dir.path, /*keep=*/10, /*minLevel=*/L::Level::Debug);
    REQUIRE(session.ok());

    ONYX_LOG_TRACE("cat", "below the file floor");
    ONYX_LOG_DEBUG("cat", "into the file");

    std::string contents = ReadWholeFile(session.path);
    CHECK(contents.find("into the file")        != std::string::npos);
    CHECK(contents.find("below the file floor") == std::string::npos);

    SL::Uninstall(session);
}

TEST_CASE("[SessionLog] Install prunes old sessions before opening the new one") {
    IsolatedLog iso;
    ScratchDir dir("install_prunes");

    Touch(dir.path / "onyx-2020-01-01_00-00-00.log");
    Touch(dir.path / "onyx-2020-01-02_00-00-00.log");
    Touch(dir.path / "onyx-2020-01-03_00-00-00.log");

    SL::Session session = SL::Install(dir.path, /*keep=*/1);
    REQUIRE(session.ok());
    SL::Uninstall(session);

    // keep=1 means one survivor, plus the session just opened.
    std::vector<std::string> left = FileNamesIn(dir.path);
    REQUIRE(left.size() == 2);
    CHECK(left[0] == "onyx-2020-01-03_00-00-00.log");
    CHECK(std::filesystem::path(session.path).filename().string() == left[1]);
}

TEST_CASE("[SessionLog] InstallAt honours an exact path, with no date in the name") {
    IsolatedLog iso;
    ScratchDir dir("install_at");

    SL::Session session = SL::InstallAt(dir.path / "run.log");
    REQUIRE(session.ok());
    CHECK(std::filesystem::path(session.path).filename().string() == "run.log");

    ONYX_LOG_INFO("cli", "headless render");
    CHECK(ReadWholeFile(session.path).find("headless render") != std::string::npos);

    SL::Uninstall(session);
}

TEST_CASE("[SessionLog] Install reports failure instead of throwing on a bad path") {
    IsolatedLog iso;
    ScratchDir dir("install_failure");

    // A file sitting where the log directory should go: creation cannot work.
    Touch(dir.path / "blocked");

    SL::Session session = SL::Install(dir.path / "blocked");
    CHECK_FALSE(session.ok());
    CHECK(session.path.empty());
}

TEST_CASE("[SessionLog] Uninstall detaches the sink and stops writing") {
    IsolatedLog iso;
    ScratchDir dir("uninstall_detaches");

    SL::Session session = SL::Install(dir.path);
    REQUIRE(session.ok());
    std::string path = session.path;

    ONYX_LOG_INFO("cat", "while attached");
    SL::Uninstall(session);
    ONYX_LOG_INFO("cat", "after detach");

    std::string contents = ReadWholeFile(path);
    CHECK(contents.find("while attached") != std::string::npos);
    CHECK(contents.find("after detach")   == std::string::npos);
    CHECK_FALSE(session.ok());
}
