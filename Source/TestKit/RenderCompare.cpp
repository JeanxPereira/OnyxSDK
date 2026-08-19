#include <Onyx/TestKit/RenderCompare.h>

// One STB_IMAGE_IMPLEMENTATION per binary that links this translation unit
// -- Onyx_TestKit is the sole owner of stb_image's implementation now that
// Tools/OnyxOracle/PngRead.cpp (which used to define it) is gone; PngWrite.cpp
// still separately owns STB_IMAGE_WRITE_IMPLEMENTATION for stb_image_write.h
// (a different header, no collision).
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cstdlib>

namespace Onyx::TestKit {

bool ReadPng(const std::filesystem::path& path, int& width, int& height,
             std::vector<uint8_t>& rgba, std::string& err) {
    int w = 0, h = 0, channelsInFile = 0;
    unsigned char* data = stbi_load(path.string().c_str(), &w, &h, &channelsInFile, 4);
    if (!data) {
        const char* reason = stbi_failure_reason();
        err = "stbi_load failed for " + path.string() + ": " + (reason ? reason : "unknown error");
        return false;
    }

    width = w;
    height = h;
    const size_t size = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
    rgba.assign(data, data + size);
    stbi_image_free(data);
    return true;
}

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

namespace {

// Builds CompareImages' pass-message: names every knob that exceeded its
// bound (there can be more than one at once), comma-separated, so a
// toolkit author staring at a failing test sees exactly which tier(s) to
// look at instead of re-deriving it from four raw numbers.
std::string DescribeFailingKnobs(const ImageCompareResult& r, const CompareTolerance& t) {
    std::string msg;
    auto append = [&msg](const std::string& part) {
        if (!msg.empty()) msg += ", ";
        msg += part;
    };
    if (r.maxChannelDelta > t.maxChannelDelta) {
        append("maxChannelDelta " + std::to_string(r.maxChannelDelta) + " > " +
               std::to_string(t.maxChannelDelta));
    }
    if (r.differingPct > t.maxDifferingPct) {
        append("differingPct " + std::to_string(r.differingPct) + " > " +
               std::to_string(t.maxDifferingPct));
    }
    if (r.highDeltaPct > t.maxHighDeltaPct) {
        append("highDeltaPct " + std::to_string(r.highDeltaPct) + " > " +
               std::to_string(t.maxHighDeltaPct));
    }
    if (r.mae > t.maxMae) {
        append("mae " + std::to_string(r.mae) + " > " + std::to_string(t.maxMae));
    }
    return msg.empty() ? "out of tolerance" : msg;
}

} // namespace

CompareResult CompareImages(const std::filesystem::path& a, const std::filesystem::path& b,
                             const CompareTolerance& tolerance) {
    CompareResult out;

    int wA = 0, hA = 0, wB = 0, hB = 0;
    std::vector<uint8_t> rgbaA, rgbaB;
    std::string errA, errB;
    const bool haveA = ReadPng(a, wA, hA, rgbaA, errA);
    const bool haveB = ReadPng(b, wB, hB, rgbaB, errB);
    if (!haveA || !haveB) {
        out.pass = false;
        out.message = "file not found or unreadable: " + std::string(haveA ? "" : (a.string() + " (" + errA + ") ")) +
                       std::string(haveB ? "" : (b.string() + " (" + errB + ")"));
        return out;
    }
    if (wA != wB || hA != hB) {
        out.pass = false;
        out.message = "dimension mismatch: " + a.string() + " is " + std::to_string(wA) + "x" +
                       std::to_string(hA) + ", " + b.string() + " is " + std::to_string(wB) + "x" +
                       std::to_string(hB);
        return out;
    }

    ImageCompareResult r = CompareRGBA(wA, hA, rgbaA, rgbaB);
    out.maxDelta = r.maxChannelDelta;
    out.differingPct = r.differingPct;
    out.highDeltaPct = r.highDeltaPct;
    out.mae = r.mae;
    out.pass = WithinTolerance(r, tolerance.maxChannelDelta, tolerance.maxDifferingPct,
                                tolerance.maxHighDeltaPct, tolerance.maxMae);
    out.message = out.pass ? "within tolerance" : DescribeFailingKnobs(r, tolerance);
    return out;
}

} // namespace Onyx::TestKit
