#include <doctest/doctest.h>

#include <Onyx/TestKit/ImageCompare.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

using namespace Onyx::TestKit;

namespace {

std::vector<std::byte> Bytes(std::initializer_list<int> values) {
    std::vector<std::byte> out;
    for (int v : values) out.push_back(static_cast<std::byte>(v));
    return out;
}

}

TEST_CASE("CompareRgba reports zero delta for identical buffers") {
    auto rgba = Bytes({10, 20, 30, 255, 40, 50, 60, 255});
    auto result = CompareRgba(2, 1, rgba, rgba);

    CHECK_FALSE(result.sizeMismatch);
    CHECK(result.maxChannelDelta == 0);
    CHECK(result.differingPixels == 0);
    CHECK(result.highDeltaPixels == 0);
    CHECK(result.totalPixels == 2);
    CHECK(result.differingPct == doctest::Approx(0.0));
    CHECK(result.highDeltaPct == doctest::Approx(0.0));
    CHECK(result.mae == doctest::Approx(0.0));
}

TEST_CASE("CompareRgba finds the single largest channel delta") {
    auto a = Bytes({0, 0, 0, 255, 100, 100, 100, 255});
    auto b = Bytes({5, 0, 0, 255, 100, 92, 100, 255});
    auto result = CompareRgba(2, 1, a, b);

    CHECK_FALSE(result.sizeMismatch);
    CHECK(result.maxChannelDelta == 8);
    CHECK(result.differingPixels == 2);
    CHECK(result.differingPct == doctest::Approx(100.0));
}

TEST_CASE("CompareRgba differingPct counts pixels, not channels") {
    std::vector<std::byte> a(16, std::byte{0});
    std::vector<std::byte> b(16, std::byte{0});
    b[0] = std::byte{1};

    auto result = CompareRgba(4, 1, a, b);
    CHECK(result.differingPixels == 1);
    CHECK(result.totalPixels == 4);
    CHECK(result.differingPct == doctest::Approx(25.0));
}

TEST_CASE("CompareRgba highDeltaPct only counts deltas strictly above kHighDeltaThreshold") {
    auto a = Bytes({0, 0, 0, 0, 0, 0, 0, 0});
    auto b = Bytes({kHighDeltaThreshold, 0, 0, 0, kHighDeltaThreshold + 1, 0, 0, 0});

    auto result = CompareRgba(2, 1, a, b);
    CHECK(result.differingPixels == 2);
    CHECK(result.highDeltaPixels == 1);
    CHECK(result.highDeltaPct == doctest::Approx(50.0));
}

TEST_CASE("CompareRgba mae is the max per-channel mean, not a global average") {
    auto a = Bytes({0, 0, 0, 255, 0, 0, 0, 255});
    auto b = Bytes({10, 0, 0, 255, 0, 0, 0, 255});

    auto result = CompareRgba(2, 1, a, b);
    CHECK(result.mae == doctest::Approx(5.0));
}

TEST_CASE("CompareRgba flags a size mismatch instead of diffing") {
    std::vector<std::byte> a(2 * 2 * 4, std::byte{0});
    std::vector<std::byte> bigger(3 * 3 * 4, std::byte{0});

    CHECK_FALSE(CompareRgba(2, 2, a, bigger).sizeMismatch);

    std::vector<std::byte> tooShort(2 * 2 * 4 - 1, std::byte{0});
    auto result = CompareRgba(2, 2, a, tooShort);
    CHECK(result.sizeMismatch);
    CHECK(result.maxChannelDelta == 0);
    CHECK(result.differingPixels == 0);
    CHECK(result.highDeltaPixels == 0);
    CHECK(result.mae == doctest::Approx(0.0));
}

TEST_CASE("WithinTolerance applies all four bounds inclusively") {
    ImageCompareResult r;
    r.maxChannelDelta = 8;
    r.differingPct = 2.0;
    r.highDeltaPct = 0.8;
    r.mae = 1.0;

    CHECK(WithinTolerance(r, {8, 2.0, 0.8, 1.0}));
    CHECK_FALSE(WithinTolerance(r, {7, 2.0, 0.8, 1.0}));
    CHECK_FALSE(WithinTolerance(r, {8, 1.9, 0.8, 1.0}));
    CHECK_FALSE(WithinTolerance(r, {8, 2.0, 0.7, 1.0}));
    CHECK_FALSE(WithinTolerance(r, {8, 2.0, 0.8, 0.9}));

    ImageCompareResult mismatch;
    mismatch.sizeMismatch = true;
    CHECK_FALSE(WithinTolerance(mismatch, {255, 100.0, 100.0, 255.0}));
}

TEST_CASE("FormatMetrics prints the four metrics with four decimals") {
    ImageCompareResult r;
    r.maxChannelDelta = 12;
    r.differingPct = 1.5;
    r.highDeltaPct = 0.25;
    r.mae = 0.125;
    CHECK(FormatMetrics(r) == "maxDelta=12 differingPct=1.5000 highDeltaPct=0.2500 mae=0.1250");
}
