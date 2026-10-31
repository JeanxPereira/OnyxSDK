#include <doctest/doctest.h>

#include <cstring>

#include "Onyx/Shell/Layer.h"

TEST_CASE("Shell layer links with a device and glfw") {
    CHECK(std::strcmp(Onyx::Shell::LayerName(), "Shell") == 0);
}
