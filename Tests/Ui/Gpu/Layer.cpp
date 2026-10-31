#include <doctest/doctest.h>

#include <cstring>

#include "Onyx/Ui/Layer.h"

TEST_CASE("Ui layer links and names itself") {
    CHECK(std::strcmp(Onyx::Ui::LayerName(), "Ui") == 0);
}
