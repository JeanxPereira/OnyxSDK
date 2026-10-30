#include <Onyx/Core/Modules/Probe.h>

#include <Onyx/Core/Vfs/OsFile.h>

#include <algorithm>

namespace Onyx::Modules {

ProbeRanking RankProbes(const std::vector<IGameModule*>& modules, const ProbeInput& input) {
    ProbeRanking ranking;
    for (IGameModule* module : modules) {
        ProbeResult result;
        try {
            result = module->Probe(input);
        } catch (...) {
            result = ProbeResult{0, "probe threw"};
        }
        result.confidence = std::clamp(result.confidence, 0, 100);
        ranking.rows.push_back({module, std::move(result)});
    }

    std::stable_sort(ranking.rows.begin(), ranking.rows.end(),
                     [](const ProbeRanking::Row& a, const ProbeRanking::Row& b) {
                         return a.result.confidence > b.result.confidence;
                     });

    if (!ranking.rows.empty() && ranking.rows[0].result.confidence >= kProbeFloor) {
        const bool beatsRunnerUp =
            ranking.rows.size() == 1 || ranking.rows[0].result.confidence > ranking.rows[1].result.confidence;
        if (beatsRunnerUp) {
            ranking.winner = ranking.rows[0].module;
        }
    }
    return ranking;
}

ProbeRanking RankProbes(const std::vector<IGameModule*>& modules, const std::filesystem::path& file) {
    constexpr size_t kHeaderBytes = 64 * 1024;

    std::vector<std::byte> header;
    uint64_t fileSize = 0;
    try {
        auto opened = Vfs::OsFile::Open(file);
        if (!opened.ok()) {
            return {};
        }
        const auto& osFile = *opened.value();
        fileSize = osFile.Size();
        header.resize(static_cast<size_t>(std::min<uint64_t>(fileSize, kHeaderBytes)));
        const size_t got = osFile.ReadAt(0, header);
        header.resize(got);
    } catch (...) {
        return {};
    }

    const ProbeInput input{file, header, fileSize};
    return RankProbes(modules, input);
}

}
