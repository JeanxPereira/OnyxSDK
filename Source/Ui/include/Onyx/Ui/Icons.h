#pragma once

#include <span>

#include "Onyx/Core/Types/Icons.h"

namespace Onyx::Ui {

struct IconEntry {
    const char* name;
    const char* glyph;
};

// Every ICON_SF_* define in SfSymbols.h, in header order.
std::span<const IconEntry> IconTable();

// The SF Symbols glyph for a Core icon; IconId::None maps to an empty string.
const char* Icon(Types::IconId id);

}
