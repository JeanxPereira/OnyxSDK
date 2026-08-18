#pragma once

// Automatic per-run log file for Onyx.
//
// Every run of the app opens its own file named after the moment it
// started -- `logs/onyx-2026-08-18_08-21-33.log` -- so a session is never
// overwritten by the next one and a bug report is a single file to attach.
//
//   Onyx::Services::SessionLog::Session s = SessionLog::Install();
//
// `Install` creates the directory, prunes older sessions down to `keep`,
// opens the file and attaches it as a log sink. The sink holds its handle
// open for the life of the process and flushes every line, so a crash keeps
// everything logged up to the crash -- which is the point of the feature.
//
// The sink carries its own minimum level (default `Level::Debug`), finer
// than what the on-screen panel keeps, so the file is useful for debugging
// after the fact without making the UI noisy. See `Log::SetMemoryMinLevel`.
//
// `InstallAt` is the same sink pointed at an exact path, for callers that
// are told where to write (a CLI's `--log run.log`) rather than wanting a
// dated name.

#include <Onyx/Services/Logger.h>

#include <ctime>
#include <filesystem>
#include <string>

namespace Onyx::Services::SessionLog {

// A handle on the installed sink. `path` is the file being written; empty
// when installation failed. Failure is reported, never thrown: logging must
// not be able to take the app down.
struct Session {
    std::string    path;
    Log::SinkToken token{0};

    // True only while the sink is attached; `Uninstall` makes this false
    // but leaves `path` readable.
    bool ok() const { return token != 0 && !path.empty(); }
};

// Default number of past sessions kept alongside the current one.
inline constexpr size_t kDefaultKeep = 10;

// `onyx-YYYY-MM-DD_HH-MM-SS.log` for the given local time. Zero padded, so
// sorting the names lexicographically sorts the sessions chronologically,
// and free of ':' so the name is valid on Windows.
std::string MakeFileName(const std::tm& when);

// One rendered file line: the canonical `[LEVEL][category] message` shape
// from `Log::FormatLine`, prefixed with `HH:MM:SS.mmm`.
std::string FormatLine(const std::tm& when, int milliseconds,
                       Log::Level lvl, std::string_view category,
                       std::string_view message);

// Deletes the oldest `onyx-*.log` files in `dir`, keeping the newest
// `keep`. Files that are not session logs are left alone. Returns how many
// were deleted; a missing directory is a no-op, not an error.
size_t Prune(const std::filesystem::path& dir, size_t keep);

// Opens `<dir>/onyx-<date>_<time>.log` and attaches it. `dir` defaults to
// `logs/` next to the executable, alongside onyx.toml and imgui.ini.
Session Install(const std::filesystem::path& dir,
                size_t     keep     = kDefaultKeep,
                Log::Level minLevel = Log::Level::Debug);
Session Install(); // `logs/` next to the executable, kDefaultKeep

// Same sink, at an exact path chosen by the caller. No date in the name and
// no pruning -- the caller named the file, so the caller owns it.
Session InstallAt(const std::filesystem::path& file,
                  Log::Level minLevel = Log::Level::Debug);

// Detaches the sink and closes the file. Leaves `session` not-ok while
// keeping `path`, so the finished session can still say where it wrote.
// Safe to call on a session that never installed.
void Uninstall(Session& session);

} // namespace Onyx::Services::SessionLog
