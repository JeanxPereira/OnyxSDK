#include <doctest/doctest.h>

#include <Onyx/Core/Layer.h>
#include <Onyx/Core/Version.h>

#include <ostream>
#include <string_view>

TEST_CASE("Core.LayerName") {
    CHECK(std::string_view(Onyx::Core::LayerName()) == "Core");
}

TEST_CASE("Core.Version") {
    CHECK(ONYX_VERSION_MAJOR == 2);
}
