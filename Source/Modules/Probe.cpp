#include <Onyx/Modules/Probe.h>
#include <algorithm>
#include <fstream>

namespace Onyx::Modules {

ProbeRanking RankProbes(const std::vector<IGameModule*>& modules,
                        const ProbeInput& input) {
    ProbeRanking result;

    // Collect all probe results from each module
    for (auto* module : modules) {
        auto probeResult = module->Probe(input);

        // Clamp confidence to [0, 100]
        probeResult.confidence = std::clamp(probeResult.confidence, 0, 100);

        result.rows.push_back({module, probeResult});
    }

    // Stable sort by confidence descending
    std::stable_sort(result.rows.begin(), result.rows.end(),
                     [](const ProbeRanking::Row& a, const ProbeRanking::Row& b) {
                         return a.result.confidence > b.result.confidence;
                     });

    // Determine winner: top row must be >= kProbeFloor AND strictly > runner-up
    if (!result.rows.empty() && result.rows[0].result.confidence >= kProbeFloor) {
        // Check if there's a tie at the top
        bool hasRunnerUp = result.rows.size() > 1;
        bool isStrictlyGreater = !hasRunnerUp ||
                                 result.rows[0].result.confidence > result.rows[1].result.confidence;

        if (isStrictlyGreater) {
            result.winner = result.rows[0].module;
        }
    }

    return result;
}

ProbeRanking RankProbes(const std::vector<IGameModule*>& modules,
                        const std::filesystem::path& file) {
    ProbeRanking emptyResult;

    try {
        // Check if file exists and get its size
        if (!std::filesystem::exists(file)) {
            return emptyResult;
        }

        auto fileSize = std::filesystem::file_size(file);

        // Read up to 64 KiB header
        constexpr size_t headerSize = 64 * 1024;
        size_t bytesToRead = std::min(static_cast<size_t>(fileSize), headerSize);

        std::vector<std::byte> headerBuffer(bytesToRead);
        std::ifstream f(file, std::ios::binary);

        if (!f.is_open()) {
            return emptyResult;
        }

        f.read(reinterpret_cast<char*>(headerBuffer.data()), bytesToRead);

        if (!f.good() && !f.eof()) {
            return emptyResult;
        }

        // Build ProbeInput and delegate
        ProbeInput input{
            .path = file,
            .header = std::span<const std::byte>(headerBuffer),
            .fileSize = fileSize
        };

        return RankProbes(modules, input);

    } catch (...) {
        // Never throw - return empty ranking on any error
        return emptyResult;
    }
}

} // namespace Onyx::Modules
