#include <doctest/doctest.h>

#include <cstring>

#include "Onyx/Ui/Icons.h"
#include "Onyx/Ui/SfSymbols.h"

TEST_CASE("Icon table lists the SF Symbols defines") {
    CHECK(Onyx::Ui::IconTable().size() >= 6000);
}

TEST_CASE("Every icon id has its own glyph") {
    using Onyx::Types::IconId;
    CHECK(std::strcmp(Onyx::Ui::Icon(IconId::None), "") == 0);
    for (int i = 1; i <= static_cast<int>(IconId::Export); ++i) {
        const char* glyph = Onyx::Ui::Icon(static_cast<IconId>(i));
        CHECK(glyph[0] != '\0');
        CHECK(std::strcmp(glyph, ICON_SF_QUESTIONMARK_CIRCLE) != 0);
    }
}
