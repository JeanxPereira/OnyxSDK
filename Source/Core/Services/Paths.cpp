#include <Onyx/Core/Services/Paths.h>

#include <cstdlib>
#include <string>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace Onyx::Services::Paths {

namespace {

std::string EnvVar(const char* name) {
#if defined(_WIN32)
    char* buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, name) != 0 || !buf) return {};
    std::string v(buf);
    std::free(buf);
    return v;
#else
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
#endif
}

}

fs::path Executable() {
#if defined(_WIN32)
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) {
            buf.resize(n);
            return fs::path(buf);
        }
        buf.resize(buf.size() * 2);
    }
#else
    std::error_code ec;
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::path() : p;
#endif
}

fs::path UserConfig(std::string_view appName) {
#if defined(_WIN32)
    std::string root = EnvVar("APPDATA");
    if (root.empty()) return fs::path(appName);
    return fs::path(root) / fs::path(appName);
#else
    std::string xdg = EnvVar("XDG_CONFIG_HOME");
    if (!xdg.empty()) return fs::path(xdg) / fs::path(appName);
    std::string home = EnvVar("HOME");
    if (home.empty()) return fs::path(appName);
    return fs::path(home) / ".config" / fs::path(appName);
#endif
}

fs::path Normalize(const fs::path& p) {
    return p.lexically_normal();
}

bool IsInside(const fs::path& base, const fs::path& p) {
    std::error_code ec;
    fs::path b = Normalize(fs::absolute(base, ec));
    fs::path t = Normalize(fs::absolute(p, ec));
    fs::path rel = t.lexically_relative(b);
    if (rel.empty()) return false;
    return *rel.begin() != "..";
}

}
