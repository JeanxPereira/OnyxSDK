#pragma once

#include <Onyx/Core/Domain/MediaKind.h>
#include <Onyx/Core/Types/Icons.h>

#include <string>

namespace Onyx::Types {

struct TypeSpec {
    std::string key;
    std::string label;
    Domain::MediaKind media = Domain::MediaKind::Unknown;
    IconId icon = IconId::None;
    Color color{0.6f, 0.6f, 0.6f, 1.0f};
};

}
