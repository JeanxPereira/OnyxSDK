#include "ImageCompare.h"

#include <cstdlib>

namespace Onyx::OracleTool {

ImageCompareResult CompareRGBA(int width, int height, const std::vector<uint8_t>& rgbaA,
                                const std::vector<uint8_t>& rgbaB) {
    ImageCompareResult r;
    if (width <= 0 || height <= 0) {
        r.sizeMismatch = true;
        return r;
    }

    const size_t total = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t needed = total * 4;
    if (rgbaA.size() < needed || rgbaB.size() < needed) {
        r.sizeMismatch = true;
        return r;
    }

    r.totalPixels = total;
    for (size_t p = 0; p < total; ++p) {
        bool differs = false;
        for (int c = 0; c < 4; ++c) {
            const int a = rgbaA[p * 4 + static_cast<size_t>(c)];
            const int b = rgbaB[p * 4 + static_cast<size_t>(c)];
            const int delta = std::abs(a - b);
            if (delta > r.maxChannelDelta) r.maxChannelDelta = delta;
            if (delta != 0) differs = true;
        }
        if (differs) ++r.differingPixels;
    }

    r.differingPct = total > 0
        ? (static_cast<double>(r.differingPixels) / static_cast<double>(total)) * 100.0
        : 0.0;
    return r;
}

bool WithinTolerance(const ImageCompareResult& result, int maxDelta, double maxPct) {
    if (result.sizeMismatch) return false;
    return result.maxChannelDelta <= maxDelta && result.differingPct <= maxPct;
}

} // namespace Onyx::OracleTool
