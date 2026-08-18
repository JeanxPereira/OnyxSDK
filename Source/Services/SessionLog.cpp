#include <Onyx/Services/SessionLog.h>

#include <Onyx/Services/PathUtils.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <system_error>
#include <vector>

namespace Onyx::Services::SessionLog {

namespace {

constexpr std::string_view kPrefix    = "onyx-";
constexpr std::string_view kExtension = ".log";

std::tm LocalTime(std::time_t raw) {
    std::tm out{};
#if defined(_WIN32)
    localtime_s(&out, &raw);
#else
    localtime_r(&raw, &out);
#endif
    return out;
}

struct Now {
    std::tm local;
    int     milliseconds;
};

Now CurrentTime() {
    auto now = std::chrono::system_clock::now();
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                   now.time_since_epoch()).count() % 1000;
    return { LocalTime(std::chrono::system_clock::to_time_t(now)),
             static_cast<int>(ms) };
}

// Only files this module produced: `onyx-*.log`. Anything else in the
// directory belongs to someone else and is never touched.
bool IsSessionFile(const std::filesystem::path& file) {
    const std::string name = file.filename().string();
    return name.size() > kPrefix.size()
        && name.compare(0, kPrefix.size(), kPrefix) == 0
        && file.extension() == kExtension;
}

// Owns the open handle for the life of the sink. Every record is flushed:
// the log is only worth having if it survives the crash it recorded.
struct FileSink {
    std::mutex    mutex;
    std::ofstream out;

    void Write(Log::Level lvl, std::string_view category, std::string_view message) {
        const Now now = CurrentTime();
        std::lock_guard<std::mutex> lock(mutex);
        if (!out.is_open()) return;
        out << FormatLine(now.local, now.milliseconds, lvl, category, message) << '\n';
        out.flush();
    }
};

Session OpenAndAttach(const std::filesystem::path& file, Log::Level minLevel) {
    std::error_code ec;
    const std::filesystem::path parent = file.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (!std::filesystem::is_directory(parent, ec)) return {};
    }

    auto sink = std::make_shared<FileSink>();
    sink->out.open(file, std::ios::app);
    if (!sink->out) return {};

    const Now now = CurrentTime();
    sink->out << std::format("=== Onyx session {:04}-{:02}-{:02} {:02}:{:02}:{:02} ===",
                             now.local.tm_year + 1900, now.local.tm_mon + 1,
                             now.local.tm_mday, now.local.tm_hour,
                             now.local.tm_min, now.local.tm_sec)
              << '\n';
    sink->out.flush();

    Session session;
    session.path  = file.string();
    session.token = Log::AddSink(
        [sink](Log::Level lvl, std::string_view category, std::string_view message) {
            sink->Write(lvl, category, message);
        },
        minLevel);
    return session;
}

} // anonymous namespace

std::string MakeFileName(const std::tm& when) {
    // Hand-formatted rather than strftime: no locale in the path, and the
    // zero padding that makes the names sort chronologically is explicit.
    return std::format("{}{:04}-{:02}-{:02}_{:02}-{:02}-{:02}{}",
                       kPrefix,
                       when.tm_year + 1900, when.tm_mon + 1, when.tm_mday,
                       when.tm_hour, when.tm_min, when.tm_sec,
                       kExtension);
}

std::string FormatLine(const std::tm& when, int milliseconds,
                       Log::Level lvl, std::string_view category,
                       std::string_view message) {
    return std::format("{:02}:{:02}:{:02}.{:03} {}",
                       when.tm_hour, when.tm_min, when.tm_sec, milliseconds,
                       Log::FormatLine(lvl, category, message));
}

size_t Prune(const std::filesystem::path& dir, size_t keep) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return 0;

    std::vector<std::filesystem::path> sessions;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        if (IsSessionFile(entry.path())) sessions.push_back(entry.path());
    }
    if (sessions.size() <= keep) return 0;

    // The dated name sorts chronologically, so the oldest are at the front.
    std::sort(sessions.begin(), sessions.end(),
              [](const std::filesystem::path& a, const std::filesystem::path& b) {
                  return a.filename().string() < b.filename().string();
              });

    size_t deleted = 0;
    for (size_t i = 0, n = sessions.size() - keep; i < n; ++i) {
        if (std::filesystem::remove(sessions[i], ec)) ++deleted;
    }
    return deleted;
}

Session Install(const std::filesystem::path& dir, size_t keep, Log::Level minLevel) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (!std::filesystem::is_directory(dir, ec)) return {};

    // Prune before opening so `keep` counts finished sessions, not this one.
    Prune(dir, keep);
    return OpenAndAttach(dir / MakeFileName(CurrentTime().local), minLevel);
}

Session Install() {
    // Next to the executable, where onyx.toml and imgui.ini already live.
    // Deliberately the executable directory rather than the resource
    // directory: on macOS the bundle's Resources folder is not ours to write.
    return Install(PathUtils::getExecutableDir() / "logs");
}

Session InstallAt(const std::filesystem::path& file, Log::Level minLevel) {
    return OpenAndAttach(file, minLevel);
}

void Uninstall(Session& session) {
    if (session.token != 0) Log::RemoveSink(session.token);
    session.token = 0;
    // `path` is deliberately kept: "where did this session's log go?" stays
    // answerable after the sink is detached.
}

} // namespace Onyx::Services::SessionLog
