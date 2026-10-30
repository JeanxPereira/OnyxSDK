#pragma once

#include <cstdint>
#include <string_view>

namespace Onyx::Types {

enum class IconId : uint16_t {
    None,
    File,
    Folder,
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
    Palette,
    Warning,
    Error,
    Info,
    Search,
    Settings,
    Play,
    Pause,
    Stop,
    Grid,
    Bone,
    Camera,
    Export,
};

struct Color {
    float r = 0.6f;
    float g = 0.6f;
    float b = 0.6f;
    float a = 1.0f;
};

std::string_view Name(IconId icon);

}
