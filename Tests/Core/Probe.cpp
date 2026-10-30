#include <doctest/doctest.h>

#include <Onyx/Core/Modules/Probe.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Onyx::Modules;

namespace {

struct FakeModule : IGameModule {
    std::string id;
    int score;
    std::string why;
    FakeModule(std::string i, int s, std::string w) : id(std::move(i)), score(s), why(std::move(w)) {}
    ModuleInfo Info() const override { return {id, id, {}, {}}; }
    ProbeResult Probe(const ProbeInput&) const override { return {score, why}; }
    void RegisterTypes(Onyx::Types::TypeRegistrar&) override {}
    void RegisterDecoders(DecoderRegistry&) override {}
    ParseResult ParseContainer(ContainerContext&) override { return {}; }
};

}

TEST_CASE("Probe.HighestScorerAboveFloorWins") {
    FakeModule a("a", 20, "weak"), b("b", 95, "magic"), c("c", 60, "plausible");
    auto r = RankProbes({&a, &b, &c}, ProbeInput{});
    REQUIRE(r.rows.size() == 3);
    CHECK(r.rows[0].result.confidence == 95);
    CHECK(r.winner == &b);
}

TEST_CASE("Probe.TieAtTheTopMeansNoWinner") {
    FakeModule a("a", 80, "x"), b("b", 80, "y");
    auto r = RankProbes({&a, &b}, ProbeInput{});
    CHECK(r.winner == nullptr);
    CHECK(r.rows.size() == 2);
}

TEST_CASE("Probe.BelowTheFloorMeansNoWinner") {
    FakeModule a("a", kProbeFloor - 1, "meh");
    auto r = RankProbes({&a}, ProbeInput{});
    CHECK(r.winner == nullptr);
}

TEST_CASE("Probe.FilePathOverloadFeedsTheHeaderToEveryModule") {
    struct SniffModule : FakeModule {
        using FakeModule::FakeModule;
        ProbeResult Probe(const ProbeInput& in) const override {
            if (in.header.size() >= 4 && std::memcmp(in.header.data(), "OBX1", 4) == 0) {
                return {95, "OBX1 magic"};
            }
            return {0, "no magic"};
        }
    };
    auto tmp = std::filesystem::temp_directory_path() / "onyx_probe_test.bin";
    {
        std::ofstream f(tmp, std::ios::binary);
        f << "OBX1rest-of-file";
    }
    SniffModule s("s", 0, "");
    FakeModule other("o", 10, "ext only");
    auto r = RankProbes(std::vector<IGameModule*>{&s, &other}, tmp);
    CHECK(r.winner == &s);
    std::filesystem::remove(tmp);
}

TEST_CASE("Probe.HeaderIsCappedAt64KiB") {
    struct SizeModule : FakeModule {
        using FakeModule::FakeModule;
        mutable size_t seen = 0;
        mutable uint64_t fileSize = 0;
        ProbeResult Probe(const ProbeInput& in) const override {
            seen = in.header.size();
            fileSize = in.fileSize;
            return {0, ""};
        }
    };
    auto tmp = std::filesystem::temp_directory_path() / "onyx_probe_big.bin";
    {
        std::ofstream f(tmp, std::ios::binary);
        std::string chunk(1024, 'x');
        for (int i = 0; i < 100; ++i) f << chunk;
    }
    SizeModule m("m", 0, "");
    RankProbes(std::vector<IGameModule*>{&m}, tmp);
    CHECK(m.seen == 64 * 1024);
    CHECK(m.fileSize == 100 * 1024);
    std::filesystem::remove(tmp);
}

TEST_CASE("Probe.ThrowingProbeIsContained") {
    struct ThrowingModule : FakeModule {
        using FakeModule::FakeModule;
        ProbeResult Probe(const ProbeInput&) const override { throw std::runtime_error("boom"); }
    };
    ThrowingModule thrower("thrower", 0, "");
    FakeModule healthy("healthy", 90, "magic");

    auto r = RankProbes(std::vector<IGameModule*>{&thrower, &healthy}, ProbeInput{});

    REQUIRE(r.rows.size() == 2);
    CHECK(r.winner == &healthy);
    bool sawThrower = false;
    for (const auto& row : r.rows) {
        if (row.module == &thrower) {
            sawThrower = true;
            CHECK(row.result.confidence == 0);
            CHECK(row.result.reason == "probe threw");
        }
    }
    CHECK(sawThrower);
}

TEST_CASE("Probe.UnreadablePathYieldsAnEmptyRanking") {
    FakeModule a("a", 95, "x");
    auto r = RankProbes(std::vector<IGameModule*>{&a}, std::filesystem::path("Z:/does/not/exist.bin"));
    CHECK(r.rows.empty());
    CHECK(r.winner == nullptr);
}
