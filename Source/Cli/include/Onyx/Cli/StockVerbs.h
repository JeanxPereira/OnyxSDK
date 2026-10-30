#pragma once

#include <Onyx/Core/Cli/Verb.h>

#include <vector>

namespace Onyx::Cli {

// probe, list, extract and decode. Every verb accepts --game <hint>.
// Without exporters, `decode --to` reports an unknown format.
std::vector<Verb> StockVerbs();

// `decode --to <format>` looks formats up in `exporters`, typically `table.Exporters()`, which must
// outlive the returned verbs.
std::vector<Verb> StockVerbs(const Exporters* exporters);

}
