#pragma once

#include <Onyx/Core/Cli/Verb.h>

#include <vector>

namespace Onyx::Cli {

// probe, list, extract and decode. Every verb accepts --game <hint>.
std::vector<Verb> StockVerbs();

}
