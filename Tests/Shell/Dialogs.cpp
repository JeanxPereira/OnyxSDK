#include <doctest/doctest.h>

#include "Onyx/Shell/Dialogs.h"

using namespace Onyx;

TEST_CASE("FilterGlobs normalises extensions to star patterns") {
    Modules::OpenFilter filter;
    filter.label = "Box";
    filter.extensions = {"obx", ".OBX", "*.box", ""};
    const auto globs = Shell::FilterGlobs(filter);
    REQUIRE(globs.size() == 3);
    CHECK(globs[0] == "*.obx");
    CHECK(globs[1] == "*.OBX");
    CHECK(globs[2] == "*.box");
}

TEST_CASE("NativeDialogs yields a dialog service") {
    CHECK(Shell::NativeDialogs() != nullptr);
}
