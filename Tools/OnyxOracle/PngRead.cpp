#include "PngRead.h"

// One STB_IMAGE_IMPLEMENTATION per target -- onyx-oracle is the only
// consumer of this file (see CMakeLists.txt), so this is the single TU
// that defines it for this binary. PngWrite.cpp already does the same
// thing for stb_image_write.h (STB_IMAGE_WRITE_IMPLEMENTATION) in this
// same directory -- separate headers, separate implementation macros, no
// collision.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Onyx::OracleTool {

bool ReadPng(const std::filesystem::path& path, int& width, int& height,
             std::vector<uint8_t>& rgba, std::string& err) {
    int w = 0, h = 0, channelsInFile = 0;
    unsigned char* data = stbi_load(path.string().c_str(), &w, &h, &channelsInFile, 4);
    if (!data) {
        const char* reason = stbi_failure_reason();
        err = "stbi_load failed for " + path.string() + ": " + (reason ? reason : "unknown error");
        return false;
    }

    width = w;
    height = h;
    const size_t size = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
    rgba.assign(data, data + size);
    stbi_image_free(data);
    return true;
}

} // namespace Onyx::OracleTool
