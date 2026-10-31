#include <doctest/doctest.h>

#include <Prism/Writer.h>

#include <cstdint>

TEST_CASE("Build returns empty when a range wraps past 4 GiB") {
    Prism::Writer w;
    w.SetPalette({{1, 2, 3, 4}});
    w.AddRange("r", Prism::Kind::Image, 0xFFFFFFF0u, 0x20u);
    CHECK(w.Build().empty());
}

TEST_CASE("Build accepts a range that ends exactly at the 32-bit limit") {
    Prism::Writer w;
    w.AddRange("r", Prism::Kind::Image, 0xFFFFFFF0u, 0x0Fu);
    CHECK_FALSE(w.Build().empty());
}
