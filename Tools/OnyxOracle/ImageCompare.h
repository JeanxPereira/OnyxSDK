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
//
// FOUR-KNOB GATE (T7 fix round, adjudicated): the initial single-pair
// (maxChannelDelta, differingPct) tolerance could not separate "a handful
// of silhouette pixels differ by a lot" (GL/Vulkan MSAA sample-position
// mismatch -- expected, harmless rasterization noise) from "the whole
// image is subtly wrong" (a real gamma/lighting/mip-filter regression) --
// the two failure modes need different knobs because they show up on
// different axes of this struct. Every scene in this milestone's corpus
// is diagnosed as pure edge noise: 100% of its high-delta pixels sit on a
// silhouette, and differingPct/mae both stay low, so the four knobs
// together gate it correctly where a single pair could not.
//
// Alpha IS compared like any other channel (not just RGB): it is a real
// blend-parity signal, not incidental -- a batch that renders opaque in
// one API and blended in the other would show up ONLY in alpha for
// otherwise-identical RGB, and the corpus's blend-stack scene actually
// exercises this in practice (its own alpha channel carries pixels that
// differ between GL and Vulkan even where RGB does not, disclosed in
// task-7-report.md's fix-round section). Excluding alpha would hide
// exactly the class of bug this gate exists to catch.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Onyx::OracleTool {

/// A channel delta strictly greater than this counts as "high" for
/// highDeltaPct below. Fixed, not caller-configurable -- it is the
/// classification boundary the four-knob gate's tiers are defined around
/// (see ImageCompareResult's own doc comment), not a per-call tolerance.
inline constexpr int kHighDeltaThreshold = 8;

/// Result of comparing two same-shape RGBA8 images. `sizeMismatch` is true
/// (and every other field left at its zero default) when either buffer is
/// shorter than width*height*4 bytes for the DECLARED width/height --
/// CompareRGBA never attempts a per-pixel diff in that case, since there
/// is no meaningful pixel correspondence. (Two images decoded from PNGs of
/// genuinely different dimensions are never even handed to CompareRGBA --
/// that check lives in Main.cpp's RunCompare, one layer up, since only it
/// has each PNG's own decoded width/height to compare before calling
/// here; CompareRGBA's own sizeMismatch is strictly about a buffer being
/// too short for the width/height ITS OWN caller declared.)
///
/// The four independent tiers the T7 fix round's amended gate checks
/// (see WithinTolerance): `maxChannelDelta` (a hard-cap tripwire only --
/// coverage-only edge deltas are bounded by local pixel contrast, so this
/// tier's detection power is deliberately weak, see WithinTolerance's own
/// comment), `differingPct` (fraction of pixels with ANY nonzero delta on
/// any channel), `highDeltaPct` (fraction of pixels with ANY channel delta
/// > kHighDeltaThreshold -- isolates the "a few pixels differ a lot" edge-
/// noise signature from a "everything differs a little" signature),
/// `mae` (per-channel mean absolute error over the WHOLE image, max across
/// the 4 channels -- catches uniform whole-image drift, e.g. gamma or
/// lighting or a mip-filter bias, that a percentage-of-pixels tier
/// structurally cannot: a 1-unit drift on literally every pixel scores
/// differingPct=100% either way, so only mae actually measures how BIG a
/// uniform drift is).
struct ImageCompareResult {
    bool   sizeMismatch    = false;
    int    maxChannelDelta = 0;   // largest |a-b| seen on any channel, any pixel
    size_t differingPixels = 0;   // pixels with a nonzero delta on at least one channel
    size_t highDeltaPixels = 0;   // pixels with a delta > kHighDeltaThreshold on at least one channel
    size_t totalPixels     = 0;
    double differingPct    = 0.0; // differingPixels / totalPixels * 100.0 (0 if totalPixels == 0)
    double highDeltaPct    = 0.0; // highDeltaPixels / totalPixels * 100.0 (0 if totalPixels == 0)
    double mae             = 0.0; // max over the 4 channels of (per-channel mean |a-b|); "1.0" reads
                                   // as one LSB of uniform whole-image drift on an 8-bit channel
};

/// Per-pixel, per-channel |a-b| over two tightly packed top-down RGBA8
/// buffers, both declared width x height. Sets `sizeMismatch` (and returns
/// early, doing no pixel work) if either buffer is shorter than
/// width*height*4 bytes -- CompareRGBA never reads out of bounds.
ImageCompareResult CompareRGBA(int width, int height, const std::vector<uint8_t>& rgbaA,
                                const std::vector<uint8_t>& rgbaB);

/// The compare command's pass/fail predicate (T7 fix round's amended
/// four-knob gate) -- pulled out as its own pure function so a test can
/// exercise the boundary conditions without constructing an
/// ImageCompareResult by hand each time. True iff NOT sizeMismatch AND
/// all four of:
///   maxChannelDelta <= maxDelta       (hard cap / tripwire -- see below)
///   differingPct    <= maxDifferingPct
///   highDeltaPct    <= maxHighDeltaPct
///   mae             <= maxMae
/// every bound inclusive.
///
/// `maxDelta` (maxChannelDelta's bound) is intentionally the WEAKEST of
/// the four: a coverage-only MSAA-resolve blend between a foreground and
/// background color is bounded by the local edge's own contrast, not by
/// anything semantically meaningful -- a k/4-covered sample on a
/// 255-magnitude edge can read up to roughly (k/4)*255, e.g. ~127 at
/// k=2/4, ~191 at k=3/4, well past any sane "this looks fine" tolerance
/// on its own. This tier exists as a tripwire against genuinely
/// catastrophic divergence (wrong geometry, wrong color entirely), NOT
/// as the gate's real discriminator -- differingPct/highDeltaPct/mae
/// carry that job, which is why this milestone's tuned value (see
/// Tools/OnyxOracle/CMakeLists.txt's VkOracleParity comment) is a wide,
/// mostly-symbolic 160, not a tight number.
bool WithinTolerance(const ImageCompareResult& result, int maxDelta, double maxDifferingPct,
                      double maxHighDeltaPct, double maxMae);

} // namespace Onyx::OracleTool
