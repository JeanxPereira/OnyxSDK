#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace Onyx::Services::Paths {

std::filesystem::path Executable();
std::filesystem::path UserConfig(std::string_view appName);
std::filesystem::path Normalize(const std::filesystem::path& p);
bool IsInside(const std::filesystem::path& base, const std::filesystem::path& p);

// `path::string()` is the ANSI code page on Windows; these two move text through UTF-8 instead.
std::filesystem::path FromUtf8(std::string_view text);
std::string ToUtf8(const std::filesystem::path& path);

}
