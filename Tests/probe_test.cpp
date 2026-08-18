#include <doctest/doctest.h>
#include <Onyx/Modules/Probe.h>
#include <fstream>
#include <cstring>
#include <filesystem>

using namespace Onyx::Modules;

namespace {
// Minimal fake: fixed score, records nothing.
struct FakeModule : IGameModule {
    std::string id; int score; std::string why;
    FakeModule(std::string i, int s, std::string w) : id(i), score(s), why(w) {}
    ModuleInfo  Info() const override { return {id, id, {}, {}}; }
    ProbeResult Probe(const ProbeInput&) const override { return {score, why}; }
    void RegisterTypes(Onyx::Types::TypeRegistrar&) override {}
    void RegisterDecoders(DecoderRegistry&) override {}
    ParseResult ParseContainer(ContainerContext&) override { return {}; }
};
} // namespace

TEST_CASE("RankProbes picks the highest scorer above the floor") {
    FakeModule a("a", 20, "weak"), b("b", 95, "magic"), c("c", 60, "plausible");
    ProbeInput in{}; // empty header is fine for fakes
    auto r = RankProbes({&a, &b, &c}, in);
    REQUIRE(r.rows.size() == 3);
    CHECK(r.rows[0].result.confidence == 95);
    CHECK(r.winner == &b);
}

TEST_CASE("A tie at the top means no winner") {
    FakeModule a("a", 80, "x"), b("b", 80, "y");
    auto r = RankProbes({&a, &b}, ProbeInput{});
    CHECK(r.winner == nullptr);
    CHECK(r.rows.size() == 2);
}

TEST_CASE("Best score below the floor means no winner") {
    FakeModule a("a", 39, "meh");
    auto r = RankProbes({&a}, ProbeInput{});
    CHECK(r.winner == nullptr);
}

TEST_CASE("File-path overload reads the header once and feeds every module") {
    // Module that scores by inspecting the header bytes.
    struct SniffModule : FakeModule {
        using FakeModule::FakeModule;
        ProbeResult Probe(const ProbeInput& in) const override {
            if (in.header.size() >= 4 &&
                std::memcmp(in.header.data(), "OBX1", 4) == 0)
                return {95, "OBX1 magic"};
            return {0, "no magic"};
        }
    };
    auto tmp = std::filesystem::temp_directory_path() / "onyx_probe_test.bin";
    { std::ofstream f(tmp, std::ios::binary); f << "OBX1rest-of-file"; }
    SniffModule s("s", 0, "");
    FakeModule  other("o", 10, "ext only");
    auto r = RankProbes(std::vector<IGameModule*>{&s, &other}, tmp);
    CHECK(r.winner == &s);
    std::filesystem::remove(tmp);
}

TEST_CASE("An unreadable path yields an empty ranking") {
    FakeModule a("a", 95, "x");
    auto r = RankProbes(std::vector<IGameModule*>{&a},
                        std::filesystem::path("Z:/does/not/exist.bin"));
    CHECK(r.rows.empty());
    CHECK(r.winner == nullptr);
}
