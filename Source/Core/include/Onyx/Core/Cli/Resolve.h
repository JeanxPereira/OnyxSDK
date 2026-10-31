#pragma once

#include <Onyx/Core/Domain/AssetEntry.h>
#include <Onyx/Core/Modules/Document.h>
#include <Onyx/Core/Modules/Selection.h>

#include <cstdint>
#include <iosfwd>
#include <string_view>
#include <vector>

namespace Onyx::Cli {

enum class ResolveStatus : uint8_t { Found, NotFound, Ambiguous, BadPath };

struct Resolution {
    ResolveStatus status = ResolveStatus::NotFound;
    const Domain::AssetEntry* entry = nullptr;  // non-null only when Found
    std::vector<Modules::NodePath> matches;     // every match when Ambiguous
};

// Resolves `target`, either `@<i>/<j>/...` (a node path) or an entry name, to an entry of `doc`.
// A name shared by several entries is Found with the earliest in depth-first order when `first` is
// set, otherwise Ambiguous with every match. A malformed path is BadPath; a path or name that
// matches nothing is NotFound.
Resolution ResolveEntry(const Modules::Document& doc, std::string_view target, bool first);

// Writes the one line a verb prints for a failed resolution, then returns false; true when Found.
//   Ambiguous: err gets `cli.ambiguous: N entries are named 'x': @0/1, @3, ...`
//   BadPath:   err gets `invalid node path: @x`
//   NotFound:  out gets `unknown entry: x`
bool ReportResolution(const Resolution& resolution, std::string_view target, std::ostream& out, std::ostream& err);

}
