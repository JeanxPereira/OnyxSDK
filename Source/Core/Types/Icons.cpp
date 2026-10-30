#include <Onyx/Core/Types/Icons.h>

#include <cstddef>
#include <iterator>

namespace Onyx::Types {

std::string_view Name(IconId icon) {
    static constexpr std::string_view kNames[] = {
        "None", "File", "Folder", "Image", "Mesh", "Material", "Skeleton", "Animation",
        "Audio", "Video", "Text", "Table", "Script", "Map", "Shader", "Container",
        "Raw", "Palette", "Warning", "Error", "Info", "Search", "Settings", "Play",
        "Pause", "Stop", "Grid", "Bone", "Camera", "Export",
    };
    const auto index = static_cast<size_t>(icon);
    return index < std::size(kNames) ? kNames[index] : kNames[0];
}

}
