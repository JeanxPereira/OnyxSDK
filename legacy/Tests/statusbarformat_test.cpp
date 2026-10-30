#include <doctest/doctest.h>

#include <Onyx/App/StatusBarFormat.h>

using namespace Onyx::App;

TEST_CASE("StatusBarFormat: FormatOpeningLine renders filename, label and rounded percent") {
    CHECK(FormatOpeningLine("mesh.obx", 0.5f, "parsing header") ==
          "opening mesh.obx: parsing header (50%)");
    // 0.005 rounds up to 1%, not down to 0%.
    CHECK(FormatOpeningLine("a.obx", 0.005f, "x") == "opening a.obx: x (1%)");
    CHECK(FormatOpeningLine("a.obx", 0.0f, "") == "opening a.obx:  (0%)");
    CHECK(FormatOpeningLine("a.obx", 1.0f, "done") == "opening a.obx: done (100%)");
}

TEST_CASE("StatusBarFormat: FormatOpeningLine clamps fraction to [0, 1]") {
    CHECK(FormatOpeningLine("a.obx", -5.0f, "x") == "opening a.obx: x (0%)");
    CHECK(FormatOpeningLine("a.obx", 5.0f, "x") == "opening a.obx: x (100%)");
}

TEST_CASE("StatusBarFormat: FormatSummaryLine pluralizes each noun by its own count") {
    CHECK(FormatSummaryLine(0, 0, 0) == "0 docs, 0 entries, 0 errors");
    CHECK(FormatSummaryLine(1, 1, 1) == "1 doc, 1 entry, 1 error");
    CHECK(FormatSummaryLine(3, 128, 2) == "3 docs, 128 entries, 2 errors");
}
