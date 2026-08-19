#pragma once
// ── ImageCompare: pure RGBA8 pixel-buffer comparison (GL/Vulkan-free) ──────
//
// Task 7 needs a PNG-aware `compare` command to gate the Vulkan renderer
// against the frozen GL goldens within a tolerance (rasterization noise,
// not semantic divergence -- see the tolerance protocol in the task
// brief). The actual PNG decode (Tools/OnyxOracle/PngRead.h) is a thin I/O
// wrapper around stb_image; the comparison math itself is kept pure here
// (raw RGBA8 buffers in, a result struct out) so it is unit-testable
// without any file I/O or GPU context -- same pattern RenderReport.h
// already established for the JSON report builder.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Onyx::OracleTool {

/// Result of comparing two same-shape RGBA8 images. `sizeMismatch` is true
/// (and every other field left at its zero default) when the two buffers'
/// declared width/height don't match, or either buffer is shorter than
/// width*height*4 bytes -- CompareRGBA never attempts a per-pixel diff in
/// that case, since there is no meaningful pixel correspondence.
struct ImageCompareResult {
    bool   sizeMismatch    = false;
    int    maxChannelDelta = 0;   // largest |a-b| seen on any channel, any pixel
    size_t differingPixels = 0;   // pixels with a nonzero delta on at least one channel
    size_t totalPixels     = 0;
    double differingPct    = 0.0; // differingPixels / totalPixels * 100.0 (0 if totalPixels == 0)
};

/// Per-pixel, per-channel |a-b| over two tightly packed top-down RGBA8
/// buffers, both declared width x height. Sets `sizeMismatch` (and returns
/// early, doing no pixel work) if either buffer is shorter than
/// width*height*4 bytes -- CompareRGBA never reads out of bounds.
ImageCompareResult CompareRGBA(int width, int height, const std::vector<uint8_t>& rgbaA,
                                const std::vector<uint8_t>& rgbaB);

/// True iff NOT sizeMismatch, maxChannelDelta <= maxDelta (inclusive), AND
/// differingPct <= maxPct (inclusive) -- the compare command's pass/fail
/// predicate, pulled out as its own pure function so a test can exercise
/// the boundary conditions without constructing an ImageCompareResult by
/// hand each time.
bool WithinTolerance(const ImageCompareResult& result, int maxDelta, double maxPct);

} // namespace Onyx::OracleTool
