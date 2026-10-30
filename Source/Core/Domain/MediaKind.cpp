#include <Onyx/Core/Domain/MediaKind.h>

namespace Onyx::Domain {

std::string_view Name(MediaKind kind) {
    switch (kind) {
    case MediaKind::Unknown: return "Unknown";
    case MediaKind::Image: return "Image";
    case MediaKind::Mesh: return "Mesh";
    case MediaKind::Material: return "Material";
    case MediaKind::Skeleton: return "Skeleton";
    case MediaKind::Animation: return "Animation";
    case MediaKind::Audio: return "Audio";
    case MediaKind::Video: return "Video";
    case MediaKind::Text: return "Text";
    case MediaKind::Table: return "Table";
    case MediaKind::Script: return "Script";
    case MediaKind::Map: return "Map";
    case MediaKind::Shader: return "Shader";
    case MediaKind::Container: return "Container";
    case MediaKind::Raw: return "Raw";
    }
    return "Unknown";
}

Types::IconId DefaultIcon(MediaKind kind) {
    using Types::IconId;
    switch (kind) {
    case MediaKind::Unknown: return IconId::File;
    case MediaKind::Image: return IconId::Image;
    case MediaKind::Mesh: return IconId::Mesh;
    case MediaKind::Material: return IconId::Material;
    case MediaKind::Skeleton: return IconId::Skeleton;
    case MediaKind::Animation: return IconId::Animation;
    case MediaKind::Audio: return IconId::Audio;
    case MediaKind::Video: return IconId::Video;
    case MediaKind::Text: return IconId::Text;
    case MediaKind::Table: return IconId::Table;
    case MediaKind::Script: return IconId::Script;
    case MediaKind::Map: return IconId::Map;
    case MediaKind::Shader: return IconId::Shader;
    case MediaKind::Container: return IconId::Folder;
    case MediaKind::Raw: return IconId::Raw;
    }
    return IconId::File;
}

}
