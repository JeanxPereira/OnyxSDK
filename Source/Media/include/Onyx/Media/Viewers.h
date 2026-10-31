#pragma once

#include <Onyx/Ui/ViewerRegistry.h>

namespace Onyx::Media {

// Registers `onyx.media` for MediaKind::Video at priority 0. It streams the request's file and range
// into a MediaView; it needs no decoder.
void RegisterMediaViewer(Ui::ViewerRegistry& registry);

}
