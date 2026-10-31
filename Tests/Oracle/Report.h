#pragma once

#include <Onyx/Render/GpuScene.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Onyx::Oracle {

// FNV-1a, 64 bit, over raw bytes.
uint64_t Fnv1a(std::span<const std::byte> bytes);

// Six decimals, locale independent; a value that rounds to zero prints without a sign.
std::string FormatFloat(float value);

// The canonical JSON report of one rendered scene: two-space indentation, '\n' line endings.
std::string BuildReport(std::string_view scene, uint32_t width, uint32_t height, uint64_t pixelHash,
                        std::span<const Render::DrawInfo> draws);

// True when both reports have the same lines apart from the value of the pixelHash line.
bool JsonEqualMaskingPixelHash(std::string_view a, std::string_view b);

}
