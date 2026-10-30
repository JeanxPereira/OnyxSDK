#pragma once

#include <Onyx/Core/Services/Diagnostics.h>

#include <iosfwd>
#include <string>
#include <string_view>

namespace Onyx::Cli {

std::string JsonEscape(std::string_view text);

const char* SeverityLabel(Services::Severity severity);

// One "[Severity] code: message" line per diagnostic; leaves the sink untouched.
void PrintDiags(const Services::DiagSink& diags, std::ostream& out);

}
