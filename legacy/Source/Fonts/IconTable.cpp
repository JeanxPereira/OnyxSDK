#include <Onyx/Fonts/IconTable.h>
#include <Onyx/Fonts/SFSymbols.h>

namespace Onyx::Fonts {

static const IconEntry kIcons[] = {
#include "Onyx/Fonts/SFSymbolsTable.inl"
};

const IconEntry* IconTable() { return kIcons; }
int              IconCount() { return int(sizeof(kIcons) / sizeof(kIcons[0])); }

} // namespace Onyx::Fonts
