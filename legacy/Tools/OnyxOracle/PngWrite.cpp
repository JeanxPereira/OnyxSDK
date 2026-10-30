#include "PngWrite.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace Onyx::OracleTool {

bool WritePng(const std::filesystem::path& path, int width, int height,
              const std::vector<uint8_t>& rgba, std::string& err) {
    if (rgba.size() < static_cast<size_t>(width) * static_cast<size_t>(height) * 4) {
        err = "pixel buffer smaller than the declared frame";
        return false;
    }
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    if (!stbi_write_png(path.string().c_str(), width, height, 4, rgba.data(), width * 4)) {
        err = "stbi_write_png failed for " + path.string();
        return false;
    }
    return true;
}

} // namespace Onyx::OracleTool
