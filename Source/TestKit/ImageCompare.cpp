#include <Onyx/TestKit/ImageCompare.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace Onyx::TestKit {

ImageCompareResult CompareRgba(uint32_t width, uint32_t height, std::span<const std::byte> a,
                               std::span<const std::byte> b) {
    ImageCompareResult r;
    const size_t total = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t needed = total * 4;
    if (total == 0 || a.size() < needed || b.size() < needed) {
        r.sizeMismatch = true;
        return r;
    }

    r.totalPixels = total;
    double channelSum[4] = {0.0, 0.0, 0.0, 0.0};

    for (size_t p = 0; p < total; ++p) {
        bool differs = false;
        bool high = false;
        for (size_t c = 0; c < 4; ++c) {
            const int va = std::to_integer<int>(a[p * 4 + c]);
            const int vb = std::to_integer<int>(b[p * 4 + c]);
            const int delta = std::abs(va - vb);
            channelSum[c] += static_cast<double>(delta);
            r.maxChannelDelta = std::max(r.maxChannelDelta, delta);
            if (delta != 0) differs = true;
            if (delta > kHighDeltaThreshold) high = true;
        }
        if (differs) ++r.differingPixels;
        if (high) ++r.highDeltaPixels;
    }

    const double totalD = static_cast<double>(total);
    r.differingPct = static_cast<double>(r.differingPixels) / totalD * 100.0;
    r.highDeltaPct = static_cast<double>(r.highDeltaPixels) / totalD * 100.0;
    for (double sum : channelSum) r.mae = std::max(r.mae, sum / totalD);
    return r;
}

bool WithinTolerance(const ImageCompareResult& result, const CompareTolerance& tolerance) {
    if (result.sizeMismatch) return false;
    return result.maxChannelDelta <= tolerance.maxChannelDelta &&
           result.differingPct <= tolerance.maxDifferingPct &&
           result.highDeltaPct <= tolerance.maxHighDeltaPct && result.mae <= tolerance.maxMae;
}

std::string FormatMetrics(const ImageCompareResult& result) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "maxDelta=%d differingPct=%.4f highDeltaPct=%.4f mae=%.4f",
                  result.maxChannelDelta, result.differingPct, result.highDeltaPct, result.mae);
    return buf;
}

}
