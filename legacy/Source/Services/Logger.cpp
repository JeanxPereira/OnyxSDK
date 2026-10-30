#include <Onyx/Services/Logger.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#  include <io.h>
#  define onyx_isatty _isatty
#  define onyx_fileno _fileno
#else
#  include <unistd.h>
#  define onyx_isatty isatty
#  define onyx_fileno fileno
#endif

namespace Onyx::Services {

// ── Shared state for the new structured surface ──────────────────────────
namespace {

// A sink plus the floor it asked for. Records below `minLevel` never reach
// `fn`, which is how the session file can capture Debug while the on-screen
// panel stays at Info.
struct SinkEntry {
    Log::SinkFn fn;
    Log::Level  minLevel{Log::Level::Trace};
};

struct LogState {
    std::mutex                              mutex;
    std::atomic<Log::Level>                 minLevel{Log::Level::Info};
    std::atomic<Log::Level>                 memoryMinLevel{Log::Level::Info};
    std::map<Log::SinkToken, SinkEntry>     sinks;
    Log::SinkToken                          nextToken{1};

    // Always-installed in-memory ring backing Logger::GetEntries().
    std::vector<LogEntry>                   memoryEntries;
    static constexpr size_t                 kMemoryCap = 4096;
};

LogState& state() {
    static LogState s;
    return s;
}

LogLevel ToLegacyLevel(Log::Level lvl) {
    switch (lvl) {
        case Log::Level::Trace: return LogLevel::Debug;
        case Log::Level::Debug: return LogLevel::Debug;
        case Log::Level::Info:  return LogLevel::Info;
        case Log::Level::Warn:  return LogLevel::Warning;
        case Log::Level::Error: return LogLevel::Error;
    }
    return LogLevel::Info;
}

Log::Level FromLegacyLevel(LogLevel lvl) {
    switch (lvl) {
        case LogLevel::Debug:   return Log::Level::Debug;
        case LogLevel::Info:    return Log::Level::Info;
        case LogLevel::Warning: return Log::Level::Warn;
        case LogLevel::Error:   return Log::Level::Error;
    }
    return Log::Level::Info;
}

std::string CurrentTimeHHMMSS() {
    auto now  = std::chrono::system_clock::now();
    std::time_t raw = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
#if defined(_WIN32)
    localtime_s(&tmBuf, &raw);
#else
    localtime_r(&raw, &tmBuf);
#endif
    char out[16] = {0};
    std::strftime(out, sizeof(out), "%H:%M:%S", &tmBuf);
    return out;
}

void AppendMemoryEntry(Log::Level lvl, std::string_view message) {
    LogState& st = state();
    if (lvl < st.memoryMinLevel.load(std::memory_order_relaxed)) return;
    // Already locked when this is called from LogString.
    LogEntry entry{ ToLegacyLevel(lvl), CurrentTimeHHMMSS(), std::string(message) };
    st.memoryEntries.push_back(std::move(entry));
    if (st.memoryEntries.size() > LogState::kMemoryCap) {
        st.memoryEntries.erase(st.memoryEntries.begin(),
            st.memoryEntries.begin() + (st.memoryEntries.size() - LogState::kMemoryCap));
    }
}

} // anonymous namespace

// ── Onyx::Logger (legacy) ──────────────────────────────────────────────────
Logger& Logger::Get() {
    static Logger instance;
    return instance;
}

void Logger::Log(LogLevel level, const char* fmt, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    Log::LogString(FromLegacyLevel(level), /*category=*/"", buffer);
}

std::vector<LogEntry> Logger::GetEntries() const {
    LogState& st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    return st.memoryEntries;
}

void Logger::Clear() {
    LogState& st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    st.memoryEntries.clear();
}

// ── Onyx::Log namespace ────────────────────────────────────────────────────
namespace Log {

const char* LevelName(Level lvl) {
    switch (lvl) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO";
        case Level::Warn:  return "WARN";
        case Level::Error: return "ERROR";
    }
    return "INFO";
}

void  SetMinLevel(Level lvl) { state().minLevel.store(lvl, std::memory_order_relaxed); }
Level GetMinLevel()          { return state().minLevel.load(std::memory_order_relaxed); }

SinkToken AddSink(SinkFn sink, Level minLevel) {
    LogState& st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    SinkToken tok = st.nextToken++;
    st.sinks.emplace(tok, SinkEntry{ std::move(sink), minLevel });
    return tok;
}

void  SetMemoryMinLevel(Level lvl) { state().memoryMinLevel.store(lvl, std::memory_order_relaxed); }
Level GetMemoryMinLevel()          { return state().memoryMinLevel.load(std::memory_order_relaxed); }

void RemoveSink(SinkToken token) {
    LogState& st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    st.sinks.erase(token);
}

void ClearSinks() {
    LogState& st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    st.sinks.clear();
}

std::string FormatLine(Level lvl, std::string_view category, std::string_view message) {
    // Canonical shape mandated by the M0.T5 AC:
    //   `[LEVEL][category] message`
    return std::format("[{}][{}] {}", LevelName(lvl), category, message);
}

void LogString(Level lvl, std::string_view category, std::string_view message) {
    if (lvl < GetMinLevel()) return;

    LogState& st = state();

    // Snapshot the sink map under the lock, then fan out without
    // holding it — keeps callbacks free to recurse safely.
    std::vector<SinkFn> active;
    {
        std::lock_guard<std::mutex> lock(st.mutex);
        active.reserve(st.sinks.size());
        for (auto& [_, entry] : st.sinks) {
            if (lvl < entry.minLevel) continue;
            active.push_back(entry.fn);
        }
        AppendMemoryEntry(lvl, message);
    }

    for (auto& sink : active) {
        try { sink(lvl, category, message); }
        catch (...) { /* swallow — never let logging crash production */ }
    }
}

SinkToken InstallStderrSink() {
    return AddSink([](Level lvl, std::string_view category, std::string_view message) {
        std::string line = FormatLine(lvl, category, message);
        std::fputs(line.c_str(), stderr);
        std::fputc('\n', stderr);
    });
}

namespace {

// Rotates path.N → path.N+1, ... up to keep `rotations` history files
// alongside `path` itself. Errors are best-effort; logging must not
// crash the host process.
void RotateFiles(const std::string& base, size_t rotations) {
    std::error_code ec;
    if (rotations == 0) {
        std::filesystem::remove(base, ec);
        return;
    }
    for (size_t i = rotations; i > 1; --i) {
        std::filesystem::path src = base + "." + std::to_string(i - 1);
        std::filesystem::path dst = base + "." + std::to_string(i);
        if (std::filesystem::exists(src, ec)) {
            std::filesystem::rename(src, dst, ec);
        }
    }
    if (std::filesystem::exists(base, ec)) {
        std::filesystem::rename(base, base + ".1", ec);
    }
}

struct RotatingFileSink {
    std::string path;
    size_t      maxBytes;
    size_t      rotations;
    std::mutex  mutex;

    void operator()(Level lvl, std::string_view category, std::string_view message) {
        std::lock_guard<std::mutex> lock(mutex);
        std::error_code ec;
        auto current = std::filesystem::file_size(path, ec);
        if (!ec && current >= maxBytes) {
            RotateFiles(path, rotations);
        }
        std::ofstream out(path, std::ios::app);
        if (!out) return;
        out << FormatLine(lvl, category, message) << '\n';
    }
};

} // anonymous namespace

SinkToken InstallRotatingFileSink(const std::string& path, size_t maxBytes, size_t rotations) {
    auto sink = std::make_shared<RotatingFileSink>();
    sink->path      = path;
    sink->maxBytes  = maxBytes;
    sink->rotations = rotations;
    return AddSink([sink](Level lvl, std::string_view category, std::string_view message) {
        (*sink)(lvl, category, message);
    });
}

} // namespace Log

} // namespace Onyx::Services
