#include <Onyx/TestKit/SceneFixtures.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace Onyx::TestKit {

namespace {

Domain::Texture MakeBlank(uint32_t size, std::string name) {
    Domain::Texture tex;
    tex.name = std::move(name);
    tex.width = size;
    tex.height = size;
    tex.format = Domain::PixelFormat::Rgba8;
    tex.pixels.resize(static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
    return tex;
}

void SetPixel(Domain::Texture& tex, uint32_t x, uint32_t y, const std::array<uint8_t, 4>& c) {
    const size_t idx = (static_cast<size_t>(y) * tex.width + x) * 4;
    for (size_t i = 0; i < 4; ++i) tex.pixels[idx + i] = static_cast<std::byte>(c[i]);
}

}

Domain::Texture MakeChecker(uint32_t size, std::array<uint8_t, 4> a, std::array<uint8_t, 4> b, std::string name) {
    Domain::Texture tex = MakeBlank(size, std::move(name));
    constexpr uint32_t kCell = 8;
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            const uint32_t cell = (x / kCell + y / kCell) % 2;
            SetPixel(tex, x, y, cell == 0 ? a : b);
        }
    }
    return tex;
}

Domain::Texture MakeGradient(uint32_t size, std::array<uint8_t, 4> top, std::array<uint8_t, 4> bottom, std::string name) {
    Domain::Texture tex = MakeBlank(size, std::move(name));
    for (uint32_t y = 0; y < size; ++y) {
        const float t = size > 1 ? static_cast<float>(y) / static_cast<float>(size - 1) : 0.0f;
        std::array<uint8_t, 4> row{};
        for (size_t c = 0; c < 4; ++c) {
            const float from = static_cast<float>(top[c]);
            const float to = static_cast<float>(bottom[c]);
            row[c] = static_cast<uint8_t>(from + (to - from) * t + 0.5f);
        }
        for (uint32_t x = 0; x < size; ++x) SetPixel(tex, x, y, row);
    }
    return tex;
}

Domain::Texture MakeSolid(uint32_t size, std::array<uint8_t, 4> c, std::string name) {
    Domain::Texture tex = MakeBlank(size, std::move(name));
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) SetPixel(tex, x, y, c);
    }
    return tex;
}

Domain::Texture MakeBumpNormal(uint32_t size, std::string name) {
    Domain::Texture tex = MakeBlank(size, std::move(name));

    constexpr uint32_t kCell = 8;
    constexpr float kHalf = static_cast<float>(kCell) / 2.0f;
    constexpr float kCenter = (static_cast<float>(kCell) - 1.0f) / 2.0f;

    auto encode = [](float n) {
        const int v = static_cast<int>(std::lround(n * 127.5f + 127.5f));
        return static_cast<uint8_t>(std::clamp(v, 0, 255));
    };

    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            const float rx = (static_cast<float>(x % kCell) - kCenter) / kHalf;
            const float ry = (static_cast<float>(y % kCell) - kCenter) / kHalf;
            const float h = std::max(0.0f, 1.0f - 4.0f * (rx * rx + ry * ry));

            float dhdx = 0.0f;
            float dhdy = 0.0f;
            if (h > 0.0f) {
                dhdx = -8.0f * rx / kHalf;
                dhdy = -8.0f * ry / kHalf;
            }

            float nx = -dhdx;
            float ny = -dhdy;
            float nz = 1.0f;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= len;
            ny /= len;
            nz /= len;
            SetPixel(tex, x, y, {encode(nx), encode(ny), encode(nz), 255});
        }
    }
    return tex;
}

}
