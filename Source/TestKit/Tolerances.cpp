#include <Onyx/TestKit/Tolerances.h>

#include <toml++/toml.hpp>

#include <string>

namespace Onyx::TestKit {

Result<CompareTolerance> ReadTolerances(const std::filesystem::path& file, std::string_view profile) {
    const std::string where = "profile '" + std::string(profile) + "' in " + file.string();

    toml::table root;
    try {
        root = toml::parse_file(file.string());
    } catch (const toml::parse_error& e) {
        return Result<CompareTolerance>::Fail({"tolerances.parse", "cannot read " + where + ": " + std::string(e.description()), 0});
    }

    const toml::table* table = root[profile].as_table();
    if (!table) {
        return Result<CompareTolerance>::Fail({"tolerances.profile", "missing " + where, 0});
    }

    CompareTolerance t;
    const auto delta = (*table)["maxChannelDelta"].value<int64_t>();
    const auto differing = (*table)["maxDifferingPct"].value<double>();
    const auto high = (*table)["maxHighDeltaPct"].value<double>();
    const auto mae = (*table)["maxMae"].value<double>();
    if (!delta || !differing || !high || !mae) {
        return Result<CompareTolerance>::Fail({"tolerances.key", "incomplete " + where + ": needs maxChannelDelta, maxDifferingPct, maxHighDeltaPct and maxMae", 0});
    }
    t.maxChannelDelta = static_cast<int>(*delta);
    t.maxDifferingPct = *differing;
    t.maxHighDeltaPct = *high;
    t.maxMae = *mae;
    return Result<CompareTolerance>::Ok(t);
}

}
