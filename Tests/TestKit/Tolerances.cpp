#include <doctest/doctest.h>

#include <Onyx/TestKit/Tolerances.h>

#include <string>

using namespace Onyx::TestKit;

TEST_CASE("ReadTolerances reads the hardware profile and rejects an unknown one") {
    const std::string file = ONYX_GOLDEN_DIR "/tolerances.toml";

    auto hardware = ReadTolerances(file, "hardware");
    REQUIRE(hardware.ok());
    CHECK(hardware.value().maxChannelDelta == 160);
    CHECK(hardware.value().maxDifferingPct == doctest::Approx(2.0));
    CHECK(hardware.value().maxHighDeltaPct == doctest::Approx(0.8));
    CHECK(hardware.value().maxMae == doctest::Approx(1.0));

    auto missing = ReadTolerances(file, "absent");
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().message.find("absent") != std::string::npos);
    CHECK(missing.error().message.find("tolerances.toml") != std::string::npos);

    CHECK_FALSE(ReadTolerances(ONYX_GOLDEN_DIR "/absent.toml", "hardware").ok());
}
