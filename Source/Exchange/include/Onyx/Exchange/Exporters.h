#pragma once

#include <Onyx/Core/Cli/Verb.h>

namespace Onyx::Exchange {

// Registers the `gltf` (Mesh), `png` (Image) and `wav` (Audio) formats of `decode --to`.
void AddExporters(Cli::VerbTable& table);

}
