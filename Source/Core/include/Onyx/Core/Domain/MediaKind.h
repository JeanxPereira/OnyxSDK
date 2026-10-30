#pragma once

#include <Onyx/Core/Types/Icons.h>

#include <cstdint>
#include <string_view>

namespace Onyx::Domain {

enum class MediaKind : uint8_t {
    Unknown,
    Image,
    Mesh,
    Material,
    Skeleton,
    Animation,
    Audio,
    Video,
    Text,
    Table,
    Script,
    Map,
    Shader,
    Container,
    Raw,
};

std::string_view Name(MediaKind kind);
Types::IconId DefaultIcon(MediaKind kind);

}
