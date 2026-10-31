#include "Onyx/Media/Viewers.h"

#include <Onyx/Media/MediaView.h>

#include <memory>
#include <utility>

namespace Onyx::Media {

void RegisterMediaViewer(Ui::ViewerRegistry& registry) {
    Ui::ViewerSpec spec;
    spec.id = "onyx.media";
    spec.target = Domain::MediaKind::Video;
    spec.priority = 0;
    spec.create = [](Ui::OpenRequest& request) -> std::unique_ptr<Ui::IDocumentView> {
        return std::make_unique<MediaView>(request.doc, request.node, request.name, request.file, request.range);
    };
    registry.Register(std::move(spec));
}

}
