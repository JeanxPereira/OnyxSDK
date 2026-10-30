#pragma once

#include <filesystem>
#include <string_view>

namespace Onyx::Services::Paths {

std::filesystem::path Executable();
std::filesystem::path UserConfig(std::string_view appName);
std::filesystem::path Normalize(const std::filesystem::path& p);
bool IsInside(const std::filesystem::path& base, const std::filesystem::path& p);

}
