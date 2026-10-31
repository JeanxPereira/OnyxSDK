#include <ostream>
#include <string_view>

#include <doctest/doctest.h>

#include "Onyx/Media/Layer.h"

TEST_CASE("Media layer name with a device") {
    CHECK(std::string_view(Onyx::Media::LayerName()) == "Media");
}
