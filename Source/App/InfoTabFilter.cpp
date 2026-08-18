#include <Onyx/App/InfoTabFilter.h>

#include <Onyx/Services/Diagnostics.h>

namespace Onyx::App {

bool DiagMentionsEntry(const Onyx::Services::Diag& diag, std::string_view entryName) {
    if (entryName.empty()) return false;
    return diag.message.find(entryName) != std::string::npos;
}

} // namespace Onyx::App
