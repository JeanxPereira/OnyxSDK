#include <Onyx/Core/Cli/Resolve.h>

#include <Onyx/Core/Modules/Selection.h>

#include <charconv>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace Onyx::Cli {

namespace {

std::optional<Modules::NodePath> ParseNodePath(std::string_view text) {
    Modules::NodePath path;
    while (true) {
        const size_t slash = text.find('/');
        const std::string_view part = text.substr(0, slash);
        uint32_t index = 0;
        const auto [end, ec] = std::from_chars(part.data(), part.data() + part.size(), index);
        if (part.empty() || ec != std::errc() || end != part.data() + part.size()) return std::nullopt;
        path.indices.push_back(index);
        if (slash == std::string_view::npos) return path;
        text.remove_prefix(slash + 1);
    }
}

std::string FormatNodePath(const Modules::NodePath& path) {
    std::string text = "@";
    for (size_t i = 0; i < path.indices.size(); ++i) {
        if (i) text += '/';
        text += std::to_string(path.indices[i]);
    }
    return text;
}

}

const Domain::AssetEntry* ResolveEntry(const Modules::Document& doc, std::string_view target, bool first,
                                       std::ostream& err) {
    if (target.starts_with('@')) {
        const auto path = ParseNodePath(target.substr(1));
        if (!path) {
            err << "invalid node path: " << target << "\n";
            return nullptr;
        }
        return Modules::Resolve(doc, *path);
    }

    const std::vector<Modules::NodePath> matches = Modules::FindAllByName(doc, target);
    if (matches.empty()) return nullptr;
    if (matches.size() > 1 && !first) {
        err << "cli.ambiguous: " << matches.size() << " entries are named '" << target << "':";
        for (size_t i = 0; i < matches.size(); ++i) err << (i ? ", " : " ") << FormatNodePath(matches[i]);
        err << "\n";
        return nullptr;
    }
    return Modules::Resolve(doc, matches.front());
}

}
