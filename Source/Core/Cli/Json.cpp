#include <Onyx/Core/Cli/Json.h>

#include <format>
#include <ostream>

namespace Onyx::Cli {

std::string JsonEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const unsigned char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += std::format("\\u{:04x}", static_cast<unsigned>(c));
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

void PrintDiags(const Services::DiagSink& diags, std::ostream& out) {
    for (const Services::Diag& d : diags.Snapshot()) {
        const char* label = d.severity == Services::Severity::Error     ? "Error"
                            : d.severity == Services::Severity::Warning ? "Warning"
                                                                        : "Info";
        out << '[' << label << "] " << d.code << ": " << d.message << '\n';
    }
}

}
