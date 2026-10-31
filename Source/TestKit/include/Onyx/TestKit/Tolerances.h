#pragma once

#include <Onyx/Core/Result.h>
#include <Onyx/TestKit/ImageCompare.h>

#include <filesystem>
#include <string_view>

namespace Onyx::TestKit {

// Reads one named table of a tolerances TOML file. A missing file, a parse error, a missing profile
// or a missing key is an error whose message names the profile and the file.
Result<CompareTolerance> ReadTolerances(const std::filesystem::path& file, std::string_view profile);

}
