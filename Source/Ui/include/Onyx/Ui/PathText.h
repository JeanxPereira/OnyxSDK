#pragma once

#include <Onyx/Core/Services/Paths.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace Onyx::Ui {

// ImGui labels and filenames are UTF-8; `path::string()` is the ANSI code
// page on Windows, so every path that reaches ImGui goes through these.
inline std::string PathToUtf8(const std::filesystem::path& path) {
    return Services::Paths::ToUtf8(path);
}

inline std::filesystem::path PathFromUtf8(std::string_view text) {
    return Services::Paths::FromUtf8(text);
}

}
