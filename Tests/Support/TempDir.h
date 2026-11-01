#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace Onyx::Test {

// A directory of its own under the system temp root, `onyx-<tag>-<pid>-<n>`, removed on destruction.
// Two suites running at once never share a name, so fixtures may use fixed file names inside it.
class TempDir {
public:
    explicit TempDir(std::string_view tag) {
        static std::atomic<std::uint32_t> counter{0};
#if defined(_WIN32)
        const long pid = static_cast<long>(_getpid());
#else
        const long pid = static_cast<long>(getpid());
#endif
        std::error_code ec;
        const auto root = std::filesystem::temp_directory_path();
        for (int attempt = 0; attempt < 64; ++attempt) {
            path_ = root / ("onyx-" + std::string(tag) + "-" + std::to_string(pid) + "-" +
                            std::to_string(counter.fetch_add(1)));
            if (std::filesystem::create_directories(path_, ec)) {
                break;
            }
            // A stale directory from a recycled pid: clear it and take the name.
            std::filesystem::remove_all(path_, ec);
            if (std::filesystem::create_directories(path_, ec)) {
                break;
            }
        }
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    const std::filesystem::path& path() const { return path_; }
    std::filesystem::path operator/(std::string_view name) const { return path_ / name; }

private:
    std::filesystem::path path_;
};

} // namespace Onyx::Test
