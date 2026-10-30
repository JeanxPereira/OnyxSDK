#pragma once

// Name/value pairs for every ICON_SF_* macro in SFSymbols.h, generated at build
// time from the header itself. Used by the icon browser in the UI Gallery and by
// the glyph debugger; consumers can use it to build their own icon pickers.

namespace Onyx::Fonts {

struct IconEntry {
    const char* name;   // "ICON_SF_GEAR"
    const char* value;  // the UTF-8 codepoint the macro expands to
};

// Pointer to a static table of IconCount() entries, in SFSymbols.h order.
const IconEntry* IconTable();
int              IconCount();

} // namespace Onyx::Fonts
