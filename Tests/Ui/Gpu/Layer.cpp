#include <doctest/doctest.h>

#include <cstring>

#include "Onyx/Ui/Layer.h"

TEST_CASE("Ui layer links with a device") {
    CHECK(std::strcmp(Onyx::Ui::LayerName(), "Ui") == 0);
}
