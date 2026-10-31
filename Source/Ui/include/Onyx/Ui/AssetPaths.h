#pragma once

#include <filesystem>
#include <string_view>

namespace Onyx::Ui {

// Looks for `relative` in <exe dir>/assets/, then <exe dir>/. Returns an empty
// path when neither holds it.
std::filesystem::path FindAsset(std::string_view relative);

}
