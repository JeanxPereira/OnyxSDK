#include <ostream>
#include <string_view>

#include <doctest/doctest.h>

#include "Onyx/Exchange/Layer.h"

TEST_CASE("Exchange layer name") {
    CHECK(std::string_view(Onyx::Exchange::LayerName()) == "Exchange");
}
