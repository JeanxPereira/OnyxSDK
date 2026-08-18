#include <Onyx/Modules/Probe.h>
#include <algorithm>
#include <fstream>

namespace Onyx::Modules {

ProbeRanking RankProbes(const std::vector<IGameModule*>& modules,
                        const ProbeInput& input) {
    ProbeRanking result;

    // Collect all probe results from each module. A single module's
    // Probe() throwing must not discard every other module's result --
    // it is contained here and scored as a zero-confidence entry so the
    // ranking still reflects every well-behaved module.
    for (auto* module : modules) {
        ProbeResult probeResult;
        try {
            probeResult = module->Probe(input);
        } catch (...) {
            probeResult = ProbeResult{0, "probe threw"};
        }

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
    std::vector<std::byte> headerBuffer;
    uint64_t fileSize = 0;

    // Only the file-read stage is guarded: a failure here (missing file,
    // permission error, filesystem exception) genuinely has no bytes to
    // hand any module, so an empty ranking is correct. Per-module probe
    // failures are contained inside the ProbeInput overload below and
    // must not be swallowed by this catch -- doing so used to discard
    // every other module's (successful) result whenever any one module
    // threw.
    try {
        // Check if file exists and get its size
        if (!std::filesystem::exists(file)) {
            return emptyResult;
        }

        fileSize = std::filesystem::file_size(file);

        // Read up to 64 KiB header
        constexpr size_t headerSize = 64 * 1024;
        size_t bytesToRead = std::min(static_cast<size_t>(fileSize), headerSize);

        headerBuffer.resize(bytesToRead);
        std::ifstream f(file, std::ios::binary);

        if (!f.is_open()) {
            return emptyResult;
        }

        f.read(reinterpret_cast<char*>(headerBuffer.data()), bytesToRead);

        if (!f.good() && !f.eof()) {
            return emptyResult;
        }
    } catch (...) {
        // Never throw - return empty ranking on any file-read error
        return emptyResult;
    }

    // Build ProbeInput and delegate (per-module throws are contained
    // inside this call, not here).
    ProbeInput input{
        .path = file,
        .header = std::span<const std::byte>(headerBuffer),
        .fileSize = fileSize
    };

    return RankProbes(modules, input);
}

} // namespace Onyx::Modules
