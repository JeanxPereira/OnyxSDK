#include "Onyx/Ui/ViewerRegistry.h"

#include <algorithm>

namespace Onyx::Ui {

namespace {

bool IsBare(Domain::MediaKind kind) {
    return kind == Domain::MediaKind::Raw || kind == Domain::MediaKind::Unknown;
}

bool KindMatches(Domain::MediaKind target, Domain::MediaKind media) {
    if (IsBare(target)) return IsBare(media);
    return target == media;
}

bool HasSlot(Domain::MediaKind kind, const Modules::Capabilities& caps) {
    using Domain::MediaKind;
    switch (kind) {
    case MediaKind::Image: return caps.image;
    case MediaKind::Text: return caps.text;
    case MediaKind::Mesh: return caps.scene;
    case MediaKind::Audio: return caps.audio;
    case MediaKind::Table: return caps.table;
    case MediaKind::Raw:
    case MediaKind::Unknown:
    case MediaKind::Video: return true;
    default: return false;
    }
}

}

void ViewerRegistry::Register(ViewerSpec spec) {
    for (ViewerSpec& existing : m_specs) {
        if (existing.id == spec.id) {
            existing = std::move(spec);
            return;
        }
    }
    m_specs.push_back(std::move(spec));
}

void ViewerRegistry::Unregister(std::string_view id) {
    std::erase_if(m_specs, [&](const ViewerSpec& s) { return s.id == id; });
}

const ViewerSpec* ViewerRegistry::Route(const Types::TypeCatalog& catalog, const Modules::DecoderRegistry& decoders,
                                        Types::TypeId type) const {
    if (type.value >= catalog.Count()) return nullptr;
    const std::string_view key = catalog.Key(type);
    const Domain::MediaKind media = catalog.Media(type);
    const Modules::Capabilities caps = decoders.Of(type);

    const ViewerSpec* byKey = nullptr;
    const ViewerSpec* byKind = nullptr;
    for (const ViewerSpec& spec : m_specs) {
        const bool named = !spec.anyCapability.empty();
        if (named && std::ranges::find(caps.any, spec.anyCapability) == caps.any.end()) continue;
        if (const auto* target = std::get_if<Types::TypeKey>(&spec.target)) {
            if (target->value == key && (!byKey || spec.priority > byKey->priority)) byKey = &spec;
        } else {
            const Domain::MediaKind kind = std::get<Domain::MediaKind>(spec.target);
            if (KindMatches(kind, media) && (named || HasSlot(kind, caps)) && (!byKind || spec.priority > byKind->priority))
                byKind = &spec;
        }
    }
    return byKey ? byKey : byKind;
}

}
