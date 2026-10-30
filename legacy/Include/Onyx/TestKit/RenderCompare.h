#pragma once
// ── TestKit::RenderCompare (spec §10) ──────────────────────────────────────
//
// This is Tools/OnyxOracle/ImageCompare.{h,cpp} (the four-knob comparator
// adjudicated and tuned during M4's Vulkan-vs-GL parity work) EXTRACTED
// verbatim into the SDK, plus the PNG decode it needs to work from paths
// instead of pre-decoded buffers, plus one new convenience wrapper
// (CompareImages) that ties the two together for a toolkit test that just
// wants "compare these two PNGs" without touching stb_image or the raw
// per-pixel struct itself. The tuning story, the per-tier reasoning and the
// numbers all carry over unchanged -- see each function's own comment below
// for the parts that came from ImageCompare.h unmodified.
//
// Design call (task-1 brief): this header depends on nothing outside
// Onyx::Core (filesystem/vector/string only) -- no Onyx::Render, no GPU
// context, no GL/Vulkan handle of any kind. TestKit is public SDK surface,
// so a public header reaching into a Tools/ header (PngRead.h) would be
// wrong; ReadPng below is PngRead.h's decode moved in-tree instead, so
// TestKit owns its own PNG decode rather than depending on tool-side code.
// PngWrite.h stays tool-side (Tools/OnyxOracle) -- nothing in the public
// TestKit interface (Goldens/DecodeSmoke/RenderCompare) ever needs to WRITE
// a PNG, only read one for comparison, so there is no consumer to justify
// moving it.
//
// Because nothing here touches Onyx::Render, Onyx_TestKit links only
// Onyx::Core -- a headless-only consumer (a decoder-only toolkit with no
// renderer at all) can use Goldens/DecodeSmoke/RenderCompare without ever
// linking a renderer. See the task-1 report for the full "one target vs.
// split" writeup.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Onyx::TestKit {

// ── PNG decode ──────────────────────────────────────────────────────────
// Moved from Tools/OnyxOracle/PngRead.{h,cpp} unchanged (stb_image, always
// forcing 4 (RGBA) output channels -- every PNG this comparator is ever
// handed came from WritePng/OffscreenTarget::Readback's own always-4-
// channel output, so decoding anything less would never happen in
// practice, but forcing it makes the contract explicit rather than
// incidental). Returns false and fills err on failure (missing file,
// corrupt/unsupported PNG); width/height/rgba are left untouched on
// failure.
bool ReadPng(const std::filesystem::path& path, int& width, int& height,
             std::vector<uint8_t>& rgba, std::string& err);

// ── Pure RGBA8 pixel-buffer comparison (GL/Vulkan-free) ────────────────────
//
// A channel delta strictly greater than this counts as "high" for
// highDeltaPct below. Fixed, not caller-configurable -- it is the
// classification boundary the four-knob gate's tiers are defined around
// (see ImageCompareResult's own doc comment), not a per-call tolerance.
inline constexpr int kHighDeltaThreshold = 8;

/// Result of comparing two same-shape RGBA8 images. `sizeMismatch` is true
/// (and every other field left at its zero default) when either buffer is
/// shorter than width*height*4 bytes for the DECLARED width/height --
/// CompareRGBA never attempts a per-pixel diff in that case, since there
/// is no meaningful pixel correspondence.
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
/// anything semantically meaningful. This tier exists as a tripwire
/// against genuinely catastrophic divergence (wrong geometry, wrong color
/// entirely), NOT as the gate's real discriminator -- differingPct/
/// highDeltaPct/mae carry that job (see Tools/OnyxOracle/CMakeLists.txt's
/// VkOracleParity comment for the tuned values and the full story).
bool WithinTolerance(const ImageCompareResult& result, int maxDelta, double maxDifferingPct,
                      double maxHighDeltaPct, double maxMae);

// ── CompareImages: the whole-file convenience wrapper (task 1, new) ────────
//
// The four tolerance knobs WithinTolerance checks, bundled as one value so
// a toolkit test (or the oracle's own `compare` command) can pass one
// argument instead of four.
struct CompareTolerance {
    int    maxChannelDelta  = 0;
    double maxDifferingPct  = 0.0;
    double maxHighDeltaPct  = 0.0;
    double maxMae           = 0.0;
};

/// Everything WithinTolerance's four fields report, plus the verdict and a
/// human-readable `message`: on failure, names the specific knob(s) that
/// exceeded their bound (or "file not found"/"dimension mismatch" for the
/// two failure modes that never reach a pixel comparison at all); on
/// success, a short confirmation. Never throws -- a missing or corrupt PNG
/// is a `pass=false` result with an explanatory message, not an exception.
struct CompareResult {
    bool   pass         = false;
    int    maxDelta      = 0;
    double differingPct  = 0.0;
    double highDeltaPct  = 0.0;
    double mae           = 0.0;
    std::string message;
};

/// Decodes both PNGs (ReadPng) and runs the four-knob gate (CompareRGBA +
/// WithinTolerance) against `tolerance`. A missing/corrupt file, or two
/// images of different declared width/height, is `pass=false` with a
/// message explaining which -- CompareRGBA is never called out-of-shape
/// (mirrors the guard Tools/OnyxOracle/Main.cpp's RunCompare has always
/// applied one layer above CompareRGBA itself).
CompareResult CompareImages(const std::filesystem::path& a, const std::filesystem::path& b,
                             const CompareTolerance& tolerance);

} // namespace Onyx::TestKit
