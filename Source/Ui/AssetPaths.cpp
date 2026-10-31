#include "Onyx/Ui/AssetPaths.h"

#include "Onyx/Core/Services/Paths.h"

#include <array>

namespace Onyx::Ui {

std::filesystem::path FindAsset(std::string_view relative) {
    namespace fs = std::filesystem;
    const fs::path rel(relative);
    const fs::path exeDir = Services::Paths::Executable().parent_path();

    const std::array<fs::path, 2> candidates = {exeDir / "assets" / rel, exeDir / rel};

    std::error_code ec;
    for (const fs::path& candidate : candidates) {
        if (fs::is_regular_file(candidate, ec))
            return candidate;
    }
    return {};
}

}
