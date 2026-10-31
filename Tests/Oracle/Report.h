#pragma once

#include <Onyx/Render/GpuScene.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <system_error>
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

// True when both paths name the same directory, whether or not either exists yet. Fails closed: when
// the filesystem cannot resolve either path it answers true, because the callers use it to refuse.
bool SameDirectory(const std::filesystem::path& a, const std::filesystem::path& b);

// The same with the path resolver supplied by the caller, so a resolution failure can be injected.
using PathResolver = std::function<std::filesystem::path(const std::filesystem::path&, std::error_code&)>;
bool SameDirectory(const std::filesystem::path& a, const std::filesystem::path& b, const PathResolver& resolve);

// The file the oracle writes into every directory it renders to. Its presence marks the PNGs there as
// the oracle's own output.
inline constexpr const char* kOutputMarker = ".OnyxOracleOutput";

// True when dir holds a PNG named after any of the scenes and no output marker: those PNGs may be
// frozen goldens, so the oracle must not write over them. Fails closed on a filesystem error.
bool HoldsForeignPng(const std::filesystem::path& dir, std::span<const std::string> names);

}
