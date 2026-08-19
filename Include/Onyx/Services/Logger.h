#pragma once

// Structured logging surface for Onyx.
//
// Call sites use the category-aware macros at the bottom of this file and
// produce output via a configurable chain of sinks:
//
//   ONYX_LOG_INFO("wad", "loaded {} entries", count);
//
// The default stderr sink renders one line per record in the canonical
// shape `[LEVEL][category] message`. Additional sinks (file, in-memory,
// custom) attach via `Log::AddSink` and detach via `Log::RemoveSink`. A
// rotating file sink can be installed once with
// `Log::InstallRotatingFileSink(path, maxBytes, rotations)`.
//
// The printf-style set (`ONYX_LOGF_DEBUG`/`INFO`/`WARN`/`ERR`) and the
// older `Onyx::Services::Logger` facade (with `LogEntry`, `GetEntries`,
// `Clear`) are retained so existing callers (StatusBar, many parser TUs)
// keep working while the codebase migrates category-by-category. Both
// paths funnel through the same sink chain, so the UI sees a unified log
// stream.
//
// ── Why every macro here is ONYX_-prefixed (v1.0.0) ──────────────────────
// Until the v1.0.0 final review these were `GOW_LOG_*` (category-aware)
// and bare `LOG_DEBUG`/`LOG_INFO`/`LOG_WARN`/`LOG_ERR` (printf-style).
// Both were wrong to freeze into a public surface, for different reasons:
//
//   - `LOG_INFO` and friends are among the most commonly #defined
//     identifiers in C++ -- glog, plog, easylogging and most engine
//     loggers claim at least one. This header is reachable from
//     <Onyx/Onyx.h>, so every consumer of the umbrella inherited them: a
//     redefinition warning at best, silent capture of THEIR logging at
//     worst.
//   - `GOW_LOG_*` put a God of War name on the main logging API of an SDK
//     whose whole premise is that it knows nothing about any specific game
//     (v1 spec §13). It was also marked "preferred", i.e. the single
//     most-typed public identifier a module author would reach for.
//
// Renaming after the 1.0 tag would have been a MAJOR-class change under
// README's own stability policy; renaming before it cost ~150 mechanical
// edits. The bare `LOG_*` spellings are still available, but only to a
// consumer who asks for them by defining ONYX_LEGACY_LOG_MACROS before
// including this header -- opt-in, never by default, so no third-party
// macro is ever silently displaced.

#include <cstdarg>
#include <cstdint>
#include <format>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Onyx::Services {

// ── Legacy logging facade (still used by ~250 call sites) ─────────────────
enum class LogLevel { Debug, Info, Warning, Error };

struct LogEntry {
    LogLevel    level;
    std::string time;
    std::string message;
};

class Logger {
public:
    static Logger& Get();

    // printf-style legacy entrypoint — routes into Log::LogString.
    void Log(LogLevel level, const char* fmt, ...);

    // UI-side accessors. Backed by an always-installed in-memory sink.
    std::vector<LogEntry> GetEntries() const;
    void Clear();

private:
    Logger() = default;
};

// ── New structured surface ────────────────────────────────────────────────
namespace Log {

enum class Level { Trace, Debug, Info, Warn, Error };

const char* LevelName(Level lvl); // "TRACE" .. "ERROR"

using SinkFn    = std::function<void(Level, std::string_view category, std::string_view message)>;
using SinkToken = uint64_t;

// Runtime minimum level. Records below this are dropped before reaching
// any sink. Default: `Level::Info`.
void  SetMinLevel(Level lvl);
Level GetMinLevel();

// Returns a token usable with `RemoveSink`. `minLevel` is the sink's own
// floor: records below it never reach this sink even when the global
// `SetMinLevel` floor lets them through. That is what lets the session
// file capture Debug while the on-screen panel stays at Info.
SinkToken AddSink(SinkFn sink, Level minLevel = Level::Trace);
void      RemoveSink(SinkToken token);
void      ClearSinks();

// Minimum level kept in the in-memory ring that backs
// `Logger::GetEntries()` (i.e. what the UI shows). Default: `Level::Info`.
// Independent of `SetMinLevel`, which is the global capture floor.
void  SetMemoryMinLevel(Level lvl);
Level GetMemoryMinLevel();

// Convenience installers for the built-in sinks. They are not installed
// automatically except for the in-memory sink, which is always present
// for the legacy `Logger::GetEntries()` accessor.
SinkToken InstallStderrSink();
SinkToken InstallRotatingFileSink(const std::string& path,
                                  size_t maxBytes   = 5u * 1024 * 1024,
                                  size_t rotations  = 3);

// Renders the canonical line shape used by the stderr/file sinks.
// Exposed so tests can verify the exact format without scraping stderr.
std::string FormatLine(Level lvl, std::string_view category, std::string_view message);

// Low-level emit. `Log::Log` (below) is the typed entry point preferred
// in production code.
void LogString(Level lvl, std::string_view category, std::string_view message);

template<typename... Args>
inline void Log(Level lvl, std::string_view category,
                std::format_string<Args...> fmtStr, Args&&... args) {
    if (lvl < GetMinLevel()) return; // fast filter; sinks repeat the check
    LogString(lvl, category, std::format(fmtStr, std::forward<Args>(args)...));
}

} // namespace Log

} // namespace Onyx::Services

// ── Category-aware macros (preferred) ────────────────────────────────────
#define ONYX_LOG_TRACE(cat, ...) ::Onyx::Services::Log::Log(::Onyx::Services::Log::Level::Trace, (cat), __VA_ARGS__)
#define ONYX_LOG_DEBUG(cat, ...) ::Onyx::Services::Log::Log(::Onyx::Services::Log::Level::Debug, (cat), __VA_ARGS__)
#define ONYX_LOG_INFO(cat, ...)  ::Onyx::Services::Log::Log(::Onyx::Services::Log::Level::Info,  (cat), __VA_ARGS__)
#define ONYX_LOG_WARN(cat, ...)  ::Onyx::Services::Log::Log(::Onyx::Services::Log::Level::Warn,  (cat), __VA_ARGS__)
#define ONYX_LOG_ERROR(cat, ...) ::Onyx::Services::Log::Log(::Onyx::Services::Log::Level::Error, (cat), __VA_ARGS__)

// ── printf-style macros (no category) ────────────────────────────────────
// The F is for "format string", and keeps these from colliding with the
// category-aware set above -- ONYX_LOG_INFO(cat, fmt, ...) and
// ONYX_LOGF_INFO(fmt, ...) are different call shapes, not two spellings of
// one macro.
#define ONYX_LOGF_DEBUG(...) ::Onyx::Services::Logger::Get().Log(::Onyx::Services::LogLevel::Debug,   __VA_ARGS__)
#define ONYX_LOGF_INFO(...)  ::Onyx::Services::Logger::Get().Log(::Onyx::Services::LogLevel::Info,    __VA_ARGS__)
#define ONYX_LOGF_WARN(...)  ::Onyx::Services::Logger::Get().Log(::Onyx::Services::LogLevel::Warning, __VA_ARGS__)
#define ONYX_LOGF_ERR(...)   ::Onyx::Services::Logger::Get().Log(::Onyx::Services::LogLevel::Error,   __VA_ARGS__)

// ── Opt-in short spellings ───────────────────────────────────────────────
// Off by default on purpose: LOG_DEBUG/LOG_INFO/LOG_WARN/LOG_ERR are
// unprefixed global macros that collide with glog, plog and most engine
// loggers, and this header is reachable from <Onyx/Onyx.h>. A consumer who
// wants the short spellings asks for them:
//
//   #define ONYX_LEGACY_LOG_MACROS
//   #include <Onyx/Onyx.h>
//
// Nothing in this repository defines it -- the whole tree uses the
// prefixed names.
#ifdef ONYX_LEGACY_LOG_MACROS
#  define LOG_DEBUG(...) ONYX_LOGF_DEBUG(__VA_ARGS__)
#  define LOG_INFO(...)  ONYX_LOGF_INFO(__VA_ARGS__)
#  define LOG_WARN(...)  ONYX_LOGF_WARN(__VA_ARGS__)
#  define LOG_ERR(...)   ONYX_LOGF_ERR(__VA_ARGS__)
#endif
