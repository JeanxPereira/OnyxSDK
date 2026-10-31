#include <doctest/doctest.h>

#include <cstring>

#include "Onyx/Shell/Layer.h"

TEST_CASE("Shell layer links") {
    CHECK(std::strcmp(Onyx::Shell::LayerName(), "Shell") == 0);
}
