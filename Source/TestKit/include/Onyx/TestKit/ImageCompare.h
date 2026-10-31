#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Onyx::TestKit {

// A channel delta strictly greater than this counts as "high" for highDeltaPct.
inline constexpr int kHighDeltaThreshold = 8;

// The outcome of comparing two tightly packed top-down RGBA8 images. sizeMismatch is set, and every
// other field left at zero, when either span is shorter than width * height * 4 bytes.
// mae is the largest of the four per-channel mean absolute errors over the whole image, so 1.0 reads
// as one LSB of uniform drift.
struct ImageCompareResult {
    bool sizeMismatch = false;
    int maxChannelDelta = 0;
    size_t differingPixels = 0;
    size_t highDeltaPixels = 0;
    size_t totalPixels = 0;
    double differingPct = 0.0;
    double highDeltaPct = 0.0;
    double mae = 0.0;
};

ImageCompareResult CompareRgba(uint32_t width, uint32_t height, std::span<const std::byte> a,
                               std::span<const std::byte> b);

struct CompareTolerance {
    int maxChannelDelta = 0;
    double maxDifferingPct = 0.0;
    double maxHighDeltaPct = 0.0;
    double maxMae = 0.0;
};

// True when the comparison did not mismatch in size and all four metrics are at or below their bounds.
bool WithinTolerance(const ImageCompareResult& result, const CompareTolerance& tolerance);

// "maxDelta=%d differingPct=%.4f highDeltaPct=%.4f mae=%.4f"
std::string FormatMetrics(const ImageCompareResult& result);

}
