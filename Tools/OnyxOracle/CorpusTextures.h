#pragma once
// ── CorpusTextures: pure (GL-free) procedural texture generators ──────────
//
// The M0 oracle corpus needs small, deterministic textures to feed into
// scenes (Task 3) without depending on any real game asset. Each generator
// below is plain CPU-side pixel math -- no GL calls, no file I/O, no
// randomness -- so it can be exercised directly from doctest as well as
// from the onyx-oracle tool.

#include <Onyx/Parsers/TextureData.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace Onyx::OracleTool {

// 8-px checker of colorA/colorB (RGBA bytes), size x size.
std::unique_ptr<Onyx::Parsers::TextureData> MakeChecker(
    uint32_t size, std::array<uint8_t, 4> a, std::array<uint8_t, 4> b,
    std::string name);

// Vertical gradient top -> bottom.
std::unique_ptr<Onyx::Parsers::TextureData> MakeGradient(
    uint32_t size, std::array<uint8_t, 4> top, std::array<uint8_t, 4> bottom,
    std::string name);

// Solid fill.
std::unique_ptr<Onyx::Parsers::TextureData> MakeSolid(
    uint32_t size, std::array<uint8_t, 4> c, std::string name);

// Tangent-space normal map: flat +Z everywhere except an 8-px bump grid
// (deterministic dome normals) so the Normal role visibly shades.
std::unique_ptr<Onyx::Parsers::TextureData> MakeBumpNormal(
    uint32_t size, std::string name);

} // namespace Onyx::OracleTool
