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
// Valid UTF-8 builds the path from its u8string. Bytes that are not valid UTF-8 never throw: they
// build the path through the narrow constructor, the native code page they most likely came from.
std::filesystem::path FromUtf8(std::string_view text);
// u8string never throws, unlike path::string() on Windows for characters outside the code page.
std::string ToUtf8(const std::filesystem::path& path);

}
