#pragma once

#include <Onyx/Ui/ViewerRegistry.h>

namespace Onyx::Ui {

// Registers onyx.image, onyx.text, onyx.hex, onyx.table and onyx.audio.
void RegisterStockViewers(ViewerRegistry& registry);

}
