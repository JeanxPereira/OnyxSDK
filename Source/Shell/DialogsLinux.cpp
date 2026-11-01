#include "Onyx/Shell/Dialogs.h"

#if !defined(_WIN32)

#include <cstdio>
#include <string>

#include "Onyx/Core/Services/Log.h"
#include "Onyx/Core/Services/Paths.h"

namespace Onyx::Shell {

namespace {

std::string Quote(std::string_view text) {
    std::string quoted = "'";
    for (const char c : text) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += '\'';
    return quoted;
}

// The first line the command prints, trimmed; empty when it fails or prints nothing.
std::string FirstLine(const std::string& command, int* status = nullptr) {
    std::string line;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        if (status) {
            *status = -1;
        }
        return line;
    }
    char buffer[4096];
    if (std::fgets(buffer, sizeof(buffer), pipe)) {
        line = buffer;
    }
    const int closed = pclose(pipe);
    if (status) {
        *status = closed;
    }
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    return line;
}

class ZenityDialogs final : public IDialogs {
public:
    ZenityDialogs() : m_available(!FirstLine("command -v zenity 2>/dev/null").empty()) {}

    std::optional<std::filesystem::path> OpenFile(std::span<const Modules::OpenFilter> filters) override {
        std::string command = "zenity --file-selection";
        for (const Modules::OpenFilter& filter : filters) {
            std::string globs;
            for (const std::string& glob : FilterGlobs(filter)) {
                globs += (globs.empty() ? "" : " ") + glob;
            }
            if (!globs.empty()) {
                command += " --file-filter=" + Quote(filter.label + " | " + globs);
            }
        }
        command += " --file-filter='All files | *'";
        return Run(command);
    }
    std::optional<std::filesystem::path> OpenFolder() override { return Run("zenity --file-selection --directory"); }
    std::optional<std::filesystem::path> SaveFile(std::string_view defaultName) override {
        return Run("zenity --file-selection --save --confirm-overwrite --filename=" + Quote(defaultName));
    }

private:
    std::optional<std::filesystem::path> Run(const std::string& command) {
        if (!m_available) {
            Services::Log::Default().Write(Services::LogLevel::Warn, "shell.dialog",
                                           "zenity is not installed; no file dialog can be shown");
            return std::nullopt;
        }
        int status = 0;
        const std::string chosen = FirstLine(command + " 2>/dev/null", &status);
        if (status != 0 || chosen.empty()) {
            return std::nullopt;
        }
        return Services::Paths::FromUtf8(chosen);
    }

    bool m_available;
};

}

std::unique_ptr<IDialogs> NativeDialogs() {
    return std::make_unique<ZenityDialogs>();
}

}

#endif
