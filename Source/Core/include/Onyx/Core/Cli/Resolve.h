#pragma once

#include <Onyx/Core/Domain/AssetEntry.h>
#include <Onyx/Core/Modules/Document.h>

#include <iosfwd>
#include <string_view>

namespace Onyx::Cli {

// Resolves `target`, either `@<i>/<j>/...` (a node path) or an entry name, to an entry of `doc`.
// A name shared by several entries resolves to the earliest in depth-first order when `first` is
// set; otherwise `err` gets `cli.ambiguous: N entries are named 'x': @0/1, @3, ...` and the result
// is null. A malformed path also writes to `err`. A name with no match returns null silently.
const Domain::AssetEntry* ResolveEntry(const Modules::Document& doc, std::string_view target, bool first,
                                       std::ostream& err);

}
