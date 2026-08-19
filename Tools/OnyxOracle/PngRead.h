#pragma once

// Shared PNG reader for the oracle's `compare` command (task 7) -- decodes
// via stb_image (vendored beside stb_image_write.h in third_party/stb,
// same directory PngWrite.cpp already includes from), always forcing 4
// (RGBA) output channels so the result is directly comparable to
// WritePng's own always-4-channel input and OffscreenTarget/HeadlessGL's
// tightly packed top-down RGBA8 readback format -- every PNG this tool
// ever writes came from one of those two sources, so decoding anything
// less than 4 channels back out would never happen in practice, but
// forcing it makes the contract explicit rather than incidental.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Onyx::OracleTool {

/// Decodes a PNG file into tightly packed top-down RGBA8 (stb_image's own
/// row order matches what WritePng/OffscreenTarget::Readback/HeadlessGL::
/// EndFrame all already produce -- no flip needed here). Returns false and
/// fills err on failure (missing file, corrupt/unsupported PNG); width/
/// height/rgba are left untouched on failure.
bool ReadPng(const std::filesystem::path& path, int& width, int& height,
             std::vector<uint8_t>& rgba, std::string& err);

} // namespace Onyx::OracleTool
