#include "ImageCompare.h"

#include <algorithm>
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

    // Per-channel running sum of |a-b|, for the whole-image MAE tier
    // (max across channels, computed once after the loop).
    double channelAbsSum[4] = {0.0, 0.0, 0.0, 0.0};

    for (size_t p = 0; p < total; ++p) {
        bool differs = false;
        bool highDelta = false;
        for (int c = 0; c < 4; ++c) {
            const int a = rgbaA[p * 4 + static_cast<size_t>(c)];
            const int b = rgbaB[p * 4 + static_cast<size_t>(c)];
            const int delta = std::abs(a - b);
            channelAbsSum[c] += static_cast<double>(delta);
            if (delta > r.maxChannelDelta) r.maxChannelDelta = delta;
            if (delta != 0) differs = true;
            if (delta > kHighDeltaThreshold) highDelta = true;
        }
        if (differs) ++r.differingPixels;
        if (highDelta) ++r.highDeltaPixels;
    }

    if (total > 0) {
        r.differingPct = (static_cast<double>(r.differingPixels) / static_cast<double>(total)) * 100.0;
        r.highDeltaPct = (static_cast<double>(r.highDeltaPixels) / static_cast<double>(total)) * 100.0;
        for (int c = 0; c < 4; ++c) {
            const double channelMae = channelAbsSum[c] / static_cast<double>(total);
            r.mae = std::max(r.mae, channelMae);
        }
    }

    return r;
}

bool WithinTolerance(const ImageCompareResult& result, int maxDelta, double maxDifferingPct,
                      double maxHighDeltaPct, double maxMae) {
    if (result.sizeMismatch) return false;
    return result.maxChannelDelta <= maxDelta && result.differingPct <= maxDifferingPct &&
           result.highDeltaPct <= maxHighDeltaPct && result.mae <= maxMae;
}

} // namespace Onyx::OracleTool
