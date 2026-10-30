#pragma once

#include <Onyx/Core/Modules/GameModule.h>

#include <filesystem>
#include <vector>

namespace Onyx::Modules {

inline constexpr int kProbeFloor = 40;

struct ProbeRanking {
    struct Row {
        IGameModule* module;
        ProbeResult result;
    };
    // Sorted by confidence descending; ties keep registration order.
    std::vector<Row> rows;
    // Set only when the top confidence is at least kProbeFloor and strictly above the runner-up.
    IGameModule* winner = nullptr;
};

// A throwing module scores 0 with reason "probe threw". An unreadable file yields an empty ranking.
ProbeRanking RankProbes(const std::vector<IGameModule*>& modules, const std::filesystem::path& file);
ProbeRanking RankProbes(const std::vector<IGameModule*>& modules, const ProbeInput& input);

}
