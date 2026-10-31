#include "Common.h"

#include <ostream>

namespace Onyx::Cli::Detail {

bool ParseArgs(std::span<const std::string_view> args, Args& parsed) {
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string_view a = args[i];
        const bool hasValue = i + 1 < args.size();
        const auto value = [&](std::string_view& into) {
            if (!hasValue) return false;
            into = args[++i];
            return true;
        };
        if (a == "--json") {
            parsed.json = true;
        } else if (a == "--strict") {
            parsed.strict = true;
        } else if (a == "--first") {
            parsed.first = true;
        } else if (a == "--game") {
            if (!value(parsed.game)) return false;
        } else if (a == "--to") {
            if (!value(parsed.to)) return false;
        } else if (a == "--out") {
            if (!value(parsed.out)) return false;
        } else if (a.starts_with("--")) {
            return false;
        } else {
            parsed.positional.push_back(a);
        }
    }
    return true;
}

bool OpenOrReport(const Modules::ScopedDocument& opened, const std::filesystem::path& path, std::ostream& out) {
    if (opened.ok()) return true;
    out << "no module accepts " << path.string() << "\n";
    return false;
}

bool IsFailed(const Domain::AssetEntry& entry) {
    return Domain::HasFlag(entry.flags, Domain::NodeFlags::Failed);
}

}

namespace Onyx::Cli {

std::vector<Verb> StockVerbs() {
    return {Detail::ProbeVerb(), Detail::ListVerb(), Detail::ExtractVerb(), Detail::DecodeVerb()};
}

}
