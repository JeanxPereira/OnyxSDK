#include <Onyx/Core/Domain/Texture.h>

namespace Onyx::Domain {

bool IsBlockCompressed(PixelFormat format) {
    switch (format) {
    case PixelFormat::Bc1:
    case PixelFormat::Bc3:
    case PixelFormat::Bc4:
    case PixelFormat::Bc5:
    case PixelFormat::Bc7:
        return true;
    case PixelFormat::Rgba8:
    case PixelFormat::RgbaF16:
        return false;
    }
    return false;
}

size_t BytesPerBlock(PixelFormat format) {
    switch (format) {
    case PixelFormat::Rgba8: return 4;
    case PixelFormat::RgbaF16: return 8;
    case PixelFormat::Bc1:
    case PixelFormat::Bc4: return 8;
    case PixelFormat::Bc3:
    case PixelFormat::Bc5:
    case PixelFormat::Bc7: return 16;
    }
    return 0;
}

size_t ExpectedByteSize(PixelFormat format, uint32_t width, uint32_t height) {
    if (!IsBlockCompressed(format)) {
        return static_cast<size_t>(width) * height * BytesPerBlock(format);
    }
    const size_t blocksX = (static_cast<size_t>(width) + 3) / 4;
    const size_t blocksY = (static_cast<size_t>(height) + 3) / 4;
    return blocksX * blocksY * BytesPerBlock(format);
}

bool Texture::IsValid() const {
    return width > 0 && height > 0 && pixels.size() == ExpectedByteSize(format, width, height);
}

}
