#pragma once

#include <filesystem>
#include <string_view>

namespace Onyx::Ui {

// Looks for `relative` in <exe dir>/assets/, then <exe dir>/, then, in builds
// that define ONYX_SOURCE_DIR, among the sources' third_party/fonts by file
// name. Returns an empty path when nothing matches.
std::filesystem::path FindAsset(std::string_view relative);

}
