#include "Onyx/Ui/AssetPaths.h"

#include "Onyx/Core/Services/Paths.h"

#include <array>

namespace Onyx::Ui {

std::filesystem::path FindAsset(std::string_view relative) {
    namespace fs = std::filesystem;
    const fs::path rel(relative);
    const fs::path exeDir = Services::Paths::Executable().parent_path();

    std::array<fs::path, 3> candidates = {exeDir / "assets" / rel, exeDir / rel, fs::path()};
#ifdef ONYX_SOURCE_DIR
    candidates[2] = fs::path(ONYX_SOURCE_DIR) / "third_party" / "fonts" / rel.filename();
#endif

    std::error_code ec;
    for (const fs::path& candidate : candidates) {
        if (!candidate.empty() && fs::is_regular_file(candidate, ec))
            return candidate;
    }
    return {};
}

}
