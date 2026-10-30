#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Onyx/Core/Result.h>

namespace Onyx::Services {

enum class LogLevel { Trace, Debug, Info, Warn, Error };

struct LogEntry {
    LogLevel level;
    std::chrono::system_clock::time_point time;
    std::string category;
    std::string message;
};

const char* LogLevelName(LogLevel level);

class Log {
public:
    using Sink = std::function<void(const LogEntry&)>;

    static Log& Default();

    void SetMinLevel(LogLevel level);
    LogLevel MinLevel() const;

    uint64_t AddSink(Sink sink, LogLevel min = LogLevel::Trace);

    // Returns only after every invocation of that sink already in flight has
    // completed, so the sink may safely capture state that dies right after.
    // Sinks are invoked without the registry lock held, so a sink may itself
    // write to the log. A sink must never call RemoveSink on its own id: it
    // would wait for itself.
    void RemoveSink(uint64_t id);

    void SetRingCapacity(size_t capacity);
    std::vector<LogEntry> Entries() const;
    void Clear();

    void Write(LogLevel level, std::string_view category, std::string_view message);

    template <class... A>
    void Write(LogLevel level, std::string_view category, std::format_string<A...> fmt, A&&... args) {
        if (level < MinLevel()) return;
        Write(level, category, std::string_view(std::format(fmt, std::forward<A>(args)...)));
    }

private:
    struct SinkEntry;

    mutable std::mutex m_mutex;
    std::atomic<LogLevel> m_minLevel{LogLevel::Info};
    std::vector<std::shared_ptr<SinkEntry>> m_sinks;
    uint64_t m_nextId = 1;
    std::deque<LogEntry> m_ring;
    size_t m_ringCapacity = 4096;
};

uint64_t InstallStderrSink(Log& log);
Result<uint64_t> InstallFileSink(Log& log, const std::filesystem::path& path,
                                size_t maxBytes = 5u << 20, int rotations = 3);

}

#define ONYX_LOG_(level, cat, ...) \
    ::Onyx::Services::Log::Default().Write(::Onyx::Services::LogLevel::level, cat, __VA_ARGS__)
#define ONYX_LOG_TRACE(cat, ...) ONYX_LOG_(Trace, cat, __VA_ARGS__)
#define ONYX_LOG_DEBUG(cat, ...) ONYX_LOG_(Debug, cat, __VA_ARGS__)
#define ONYX_LOG_INFO(cat, ...) ONYX_LOG_(Info, cat, __VA_ARGS__)
#define ONYX_LOG_WARN(cat, ...) ONYX_LOG_(Warn, cat, __VA_ARGS__)
#define ONYX_LOG_ERROR(cat, ...) ONYX_LOG_(Error, cat, __VA_ARGS__)
