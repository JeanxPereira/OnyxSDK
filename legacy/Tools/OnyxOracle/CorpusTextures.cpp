#include "CorpusTextures.h"

#include <algorithm>
#include <cmath>

namespace Onyx::OracleTool {

using Onyx::Parsers::TextureData;

namespace {

void SetPixel(TextureData& tex, uint32_t x, uint32_t y, const std::array<uint8_t, 4>& c) {
    size_t idx = (static_cast<size_t>(y) * tex.width + x) * 4;
    tex.pixels[idx + 0] = c[0];
    tex.pixels[idx + 1] = c[1];
    tex.pixels[idx + 2] = c[2];
    tex.pixels[idx + 3] = c[3];
}

std::unique_ptr<TextureData> MakeBlank(uint32_t size, std::string name) {
    auto tex = std::make_unique<TextureData>();
    tex->name = std::move(name);
    tex->width = size;
    tex->height = size;
    tex->pixels.resize(static_cast<size_t>(size) * static_cast<size_t>(size) * 4);
    return tex;
}

} // namespace

std::unique_ptr<TextureData> MakeChecker(uint32_t size, std::array<uint8_t, 4> a,
                                          std::array<uint8_t, 4> b, std::string name) {
    auto tex = MakeBlank(size, std::move(name));

    constexpr uint32_t kCell = 8;
    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            uint32_t cell = (x / kCell + y / kCell) % 2;
            SetPixel(*tex, x, y, (cell == 0) ? a : b);
        }
    }
    return tex;
}

std::unique_ptr<TextureData> MakeGradient(uint32_t size, std::array<uint8_t, 4> top,
                                           std::array<uint8_t, 4> bottom, std::string name) {
    auto tex = MakeBlank(size, std::move(name));

    for (uint32_t y = 0; y < size; ++y) {
        float t = (size > 1) ? static_cast<float>(y) / static_cast<float>(size - 1) : 0.0f;
        std::array<uint8_t, 4> row{};
        for (size_t c = 0; c < 4; ++c) {
            float a = static_cast<float>(top[c]);
            float b = static_cast<float>(bottom[c]);
            row[c] = static_cast<uint8_t>(a + (b - a) * t + 0.5f);
        }
        for (uint32_t x = 0; x < size; ++x) {
            SetPixel(*tex, x, y, row);
        }
    }
    return tex;
}

std::unique_ptr<TextureData> MakeSolid(uint32_t size, std::array<uint8_t, 4> c, std::string name) {
    auto tex = MakeBlank(size, std::move(name));

    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            SetPixel(*tex, x, y, c);
        }
    }
    return tex;
}

std::unique_ptr<TextureData> MakeBumpNormal(uint32_t size, std::string name) {
    auto tex = MakeBlank(size, std::move(name));

    constexpr uint32_t kCell = 8;
    constexpr float kHalf = static_cast<float>(kCell) / 2.0f; // 4.0
    constexpr float kCenter = (static_cast<float>(kCell) - 1.0f) / 2.0f; // 3.5

    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            uint32_t lx = x % kCell;
            uint32_t ly = y % kCell;

            // Local coordinates normalized to the cell's half-width, centered
            // on the 8x8 cell so the dome peaks at the cell center.
            float rx = (static_cast<float>(lx) - kCenter) / kHalf;
            float ry = (static_cast<float>(ly) - kCenter) / kHalf;
            float r2 = rx * rx + ry * ry;
            float h = std::max(0.0f, 1.0f - 4.0f * r2);

            float dhdx = 0.0f;
            float dhdy = 0.0f;
            if (h > 0.0f) {
                // d/drx [1 - 4*(rx^2+ry^2)] = -8*rx, chained through rx = (lx-c)/half.
                dhdx = -8.0f * rx / kHalf;
                dhdy = -8.0f * ry / kHalf;
            }

            float nx = -dhdx;
            float ny = -dhdy;
            float nz = 1.0f;
            float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= len;
            ny /= len;
            nz /= len;

            auto Encode = [](float n) -> uint8_t {
                int v = static_cast<int>(std::lround(n * 127.5f + 127.5f));
                v = std::clamp(v, 0, 255);
                return static_cast<uint8_t>(v);
            };

            SetPixel(*tex, x, y, {Encode(nx), Encode(ny), Encode(nz), 255});
        }
    }
    return tex;
}

} // namespace Onyx::OracleTool
