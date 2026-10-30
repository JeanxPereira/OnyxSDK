#include <doctest/doctest.h>

#include <Onyx/Core/Services/Log.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

using namespace Onyx::Services;
namespace fs = std::filesystem;

namespace {

struct Counted {
    static inline int formatted = 0;
};

fs::path TempDir(const char* name) {
    fs::path d = fs::temp_directory_path() / "OnyxLogTests" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

}

template <>
struct std::formatter<Counted> {
    constexpr auto parse(std::format_parse_context& c) { return c.begin(); }
    auto format(const Counted&, std::format_context& c) const {
        ++Counted::formatted;
        return std::format_to(c.out(), "counted");
    }
};

TEST_CASE("Log.FiltersBelowMinLevelBeforeFormatting") {
    Log log;
    log.SetMinLevel(LogLevel::Warn);
    int received = 0;
    log.AddSink([&](const LogEntry&) { ++received; });
    Counted::formatted = 0;

    log.Write(LogLevel::Debug, "t", "value {}", Counted{});
    CHECK(Counted::formatted == 0);
    CHECK(received == 0);

    log.Write(LogLevel::Error, "t", "value {}", Counted{});
    CHECK(Counted::formatted == 1);
    CHECK(received == 1);
}

TEST_CASE("Log.RingEvictsOldestAtCapacity") {
    Log log;
    log.SetMinLevel(LogLevel::Trace);
    log.SetRingCapacity(3);
    for (int i = 0; i < 5; ++i) log.Write(LogLevel::Info, "t", std::format("m{}", i));
    auto e = log.Entries();
    REQUIRE(e.size() == 3);
    CHECK(e[0].message == "m2");
    CHECK(e[1].message == "m3");
    CHECK(e[2].message == "m4");
}

TEST_CASE("Log.SinkRemovedStopsReceiving") {
    Log log;
    int received = 0;
    auto id = log.AddSink([&](const LogEntry&) { ++received; });
    log.Write(LogLevel::Info, "t", "a");
    CHECK(received == 1);
    log.RemoveSink(id);
    log.Write(LogLevel::Info, "t", "b");
    CHECK(received == 1);
}

TEST_CASE("Log.FileSinkRotatesBySize") {
    auto dir = TempDir("Rotate");
    auto path = dir / "log";
    {
        Log log;
        log.SetMinLevel(LogLevel::Trace);
        InstallFileSink(log, path, 200, 2);
        for (int i = 0; i < 100; ++i) log.Write(LogLevel::Info, "t", std::format("line {}", i));
    }
    CHECK(fs::exists(path));
    CHECK(fs::exists(dir / "log.1"));
    CHECK(fs::exists(dir / "log.2"));
    CHECK_FALSE(fs::exists(dir / "log.3"));
}

TEST_CASE("Log.FileSinkKeepsHandleOpen") {
    auto dir = TempDir("Handle");
    Log log;
    log.SetMinLevel(LogLevel::Trace);
    InstallFileSink(log, dir / "log");
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 1000; ++i) log.Write(LogLevel::Info, "t", "a line of log text");
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ms < 500);
}

TEST_CASE("Log.RemoveSinkWaitsForInFlightCall") {
    Log log;
    std::mutex m;
    std::condition_variable cv;
    bool entered = false;
    bool open = false;
    std::atomic<bool> removed{false};

    auto id = log.AddSink([&](const LogEntry&) {
        std::unique_lock lock(m);
        entered = true;
        cv.notify_all();
        cv.wait(lock, [&] { return open; });
    });

    std::thread writer([&] { log.Write(LogLevel::Info, "t", "blocked"); });
    {
        std::unique_lock lock(m);
        REQUIRE(cv.wait_for(lock, std::chrono::seconds(10), [&] { return entered; }));
    }

    std::thread remover([&] {
        log.RemoveSink(id);
        removed = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK_FALSE(removed.load());

    {
        std::lock_guard lock(m);
        open = true;
    }
    cv.notify_all();
    writer.join();
    remover.join();
    CHECK(removed.load());
}

TEST_CASE("Log.SinkMayWriteToTheLogItself") {
    Log log;
    log.SetMinLevel(LogLevel::Trace);
    int nested = 0;
    log.AddSink([&](const LogEntry& e) {
        if (e.category == "outer") log.Write(LogLevel::Info, "inner", "from sink");
        else ++nested;
    });
    log.Write(LogLevel::Info, "outer", "go");
    CHECK(nested == 1);
}

TEST_CASE("Log.FileSinkOnUnwritablePathFails") {
    auto dir = TempDir("Unwritable");
    auto blocker = dir / "file";
    { std::ofstream(blocker) << "x"; }
    Log log;
    auto r = InstallFileSink(log, blocker / "child" / "log");
    CHECK_FALSE(r.ok());
    log.Write(LogLevel::Info, "t", "still fine");
    CHECK(log.Entries().size() == 1);
}
