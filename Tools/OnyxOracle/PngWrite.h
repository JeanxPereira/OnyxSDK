#pragma once

// Shared PNG writer for both oracle render paths -- moved out of
// HeadlessGL (task 4) so the Vulkan path (Main.cpp's --vk-smoke,
// OffscreenTarget-driven) can write its own smoke-test PNGs without
// reaching into HeadlessGL's GL-only class. HeadlessGL::WritePng's exact
// implementation (stb_image_write, create-parent-dirs-first) moved here
// unchanged; HeadlessGL's render-corpus callers now go through this
// instead.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Onyx::OracleTool {

/// Writes tightly packed top-down RGBA as a PNG via stb_image_write.
/// Creates path's parent directories first if they don't already exist.
/// Returns false and fills err on failure (rgba smaller than the declared
/// width*height*4, or stbi_write_png itself failing).
bool WritePng(const std::filesystem::path& path, int width, int height,
              const std::vector<uint8_t>& rgba, std::string& err);

} // namespace Onyx::OracleTool
