#include <Onyx/App/StatusBarFormat.h>

#include <algorithm>

namespace Onyx::App {

std::string FormatOpeningLine(std::string_view filename, float fraction, std::string_view label) {
    float clamped = std::clamp(fraction, 0.0f, 1.0f);
    int pct = static_cast<int>(clamped * 100.0f + 0.5f);

    std::string out;
    out.reserve(filename.size() + label.size() + 24);
    out += "opening ";
    out += filename;
    out += ": ";
    out += label;
    out += " (";
    out += std::to_string(pct);
    out += "%)";
    return out;
}

std::string FormatSummaryLine(size_t docCount, size_t entryCount, size_t errorCount) {
    std::string out;
    out += std::to_string(docCount);
    out += docCount == 1 ? " doc, " : " docs, ";
    out += std::to_string(entryCount);
    out += entryCount == 1 ? " entry, " : " entries, ";
    out += std::to_string(errorCount);
    out += errorCount == 1 ? " error" : " errors";
    return out;
}

} // namespace Onyx::App
