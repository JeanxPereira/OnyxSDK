#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Onyx::Ui {

// ImGui labels and filenames are UTF-8; `path::string()` is the ANSI code
// page on Windows, so every path that reaches ImGui goes through these.
inline std::string PathToUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

inline std::filesystem::path PathFromUtf8(std::string_view text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

}
