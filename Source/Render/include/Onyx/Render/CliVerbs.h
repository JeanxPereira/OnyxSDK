#pragma once

#include <Onyx/Core/Cli/Verb.h>

#include <vector>

namespace Onyx::Render {

// The `render` verb: renders a scene entry to a PNG on a default Gpu::Device, framed from the +Z side
// over the scene bounds. The PNG is written through the table's "png" exporter; without one the verb
// prints `render.noPng` and returns kUsage. A scene that does not decode prints `render.noScene` and
// returns kPartial, and a machine without a device prints `gpu.noDevice` and returns kPartial.
std::vector<Cli::Verb> CliVerbs();

}
