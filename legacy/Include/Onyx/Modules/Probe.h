#pragma once
#include <Onyx/Modules/GameModule.h>

namespace Onyx::Modules {

inline constexpr int kProbeFloor = 40;   // best score below this => nobody wins

struct ProbeRanking {
    struct Row { IGameModule* module; ProbeResult result; };
    std::vector<Row> rows;        // sorted by confidence, descending; ties keep
                                  // registration order (stable sort)
    // Winner: the single top row when its confidence >= kProbeFloor AND it
    // is strictly greater than the runner-up (a tie means ambiguity).
    IGameModule* winner = nullptr;
};

// Reads the header once and asks every module. Never throws; an unreadable
// file yields an empty ranking (no winner).
ProbeRanking RankProbes(const std::vector<IGameModule*>& modules,
                        const std::filesystem::path& file);

// Same, for callers that already hold the bytes (tests, archives).
ProbeRanking RankProbes(const std::vector<IGameModule*>& modules,
                        const ProbeInput& input);

} // namespace Onyx::Modules
