#include <Onyx/Core/Services/Paths.h>

#include <cstdlib>
#include <cwctype>
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
    if (!root.empty() && fs::path(root).is_absolute()) return fs::path(root) / fs::path(appName);
    std::string profile = EnvVar("USERPROFILE");
    if (profile.empty() || !fs::path(profile).is_absolute()) return {};
    return fs::path(profile) / "AppData" / "Roaming" / fs::path(appName);
#else
    std::string xdg = EnvVar("XDG_CONFIG_HOME");
    if (!xdg.empty() && fs::path(xdg).is_absolute()) return fs::path(xdg) / fs::path(appName);
    std::string home = EnvVar("HOME");
    if (home.empty() || !fs::path(home).is_absolute()) return {};
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
#if defined(_WIN32)
    auto fold = [](const fs::path& x) {
        std::wstring w = x.wstring();
        for (wchar_t& c : w) c = static_cast<wchar_t>(::towlower(c));
        return fs::path(w).make_preferred();
    };
    b = fold(b);
    t = fold(t);
#endif
    fs::path rel = t.lexically_relative(b);
    if (rel.empty()) return false;
    return *rel.begin() != "..";
}

fs::path FromUtf8(std::string_view text) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string ToUtf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

}
