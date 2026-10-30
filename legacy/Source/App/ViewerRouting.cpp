#include <Onyx/App/ViewerRouting.h>
#include <Onyx/Modules/DecoderRegistry.h>

namespace Onyx::App {

ViewerKind RouteForType(const Onyx::Modules::DecoderRegistry& decoders, Onyx::Types::TypeId typeId) {
    if (decoders.HasScene(typeId)) return ViewerKind::Scene;
    if (decoders.HasImage(typeId)) return ViewerKind::Image;
    if (decoders.HasText(typeId))  return ViewerKind::Text;
    return ViewerKind::None;
}

} // namespace Onyx::App
