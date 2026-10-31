#include "Report.h"

#include <charconv>
#include <cstdio>
#include <system_error>
#include <vector>

namespace Onyx::Oracle {

namespace {

const char* BlendName(Domain::BlendMode mode) {
    switch (mode) {
    case Domain::BlendMode::Opaque: return "Opaque";
    case Domain::BlendMode::Alpha: return "Alpha";
    case Domain::BlendMode::Additive: return "Additive";
    case Domain::BlendMode::Subtractive: return "Subtractive";
    case Domain::BlendMode::EnvMap: return "EnvMap";
    }
    return "Opaque";
}

void AppendString(std::string& out, std::string_view text) {
    out += '"';
    for (const unsigned char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                out += buffer;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
}

void AppendBool(std::string& out, bool value) {
    out += value ? "true" : "false";
}

std::vector<std::string_view> SplitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    size_t start = 0;
    while (start < text.size()) {
        const size_t newline = text.find('\n', start);
        if (newline == std::string_view::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, newline - start));
        start = newline + 1;
    }
    return lines;
}

}

uint64_t Fnv1a(std::span<const std::byte> bytes) {
    uint64_t hash = 14695981039346656037ull;
    for (const std::byte b : bytes) {
        hash ^= static_cast<uint8_t>(b);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string FormatFloat(float value) {
    // to_chars ignores LC_NUMERIC, which printf would honour.
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed, 6);
    if (result.ec != std::errc()) {
        return "0.000000";
    }
    std::string text(buffer, result.ptr);
    if (text.front() == '-' && text.find_first_not_of("-0.") == std::string::npos) {
        text.erase(0, 1);
    }
    return text;
}

std::string BuildReport(std::string_view scene, uint32_t width, uint32_t height, uint64_t pixelHash,
                        std::span<const Render::DrawInfo> draws) {
    std::string out = "{\n  \"scene\": ";
    AppendString(out, scene);
    out += ",\n  \"width\": " + std::to_string(width);
    out += ",\n  \"height\": " + std::to_string(height);
    out += ",\n  \"pixelHash\": " + std::to_string(pixelHash) + ",\n";
    if (draws.empty()) {
        out += "  \"draws\": []\n";
    } else {
        out += "  \"draws\": [\n";
        for (size_t i = 0; i < draws.size(); ++i) {
            const Render::DrawInfo& d = draws[i];
            out += "    {\n      \"name\": ";
            AppendString(out, d.name);
            out += ",\n      \"material\": " + std::to_string(d.materialIndex);
            out += ",\n      \"vertexCount\": " + std::to_string(d.vertexCount);
            out += ",\n      \"triangleCount\": " + std::to_string(d.triangleCount);
            out += std::string(",\n      \"blend\": \"") + BlendName(d.blend) + "\"";
            out += ",\n      \"skinned\": ";
            AppendBool(out, d.skinned);
            out += ",\n      \"sky\": ";
            AppendBool(out, d.sky);
            out += ",\n      \"texturesBound\": " + std::to_string(d.texturesBound);
            out += ",\n      \"jointCount\": " + std::to_string(d.jointCount);
            out += i + 1 < draws.size() ? "\n    },\n" : "\n    }\n";
        }
        out += "  ]\n";
    }
    out += "}\n";
    return out;
}

bool JsonEqualMaskingPixelHash(std::string_view a, std::string_view b) {
    const auto linesA = SplitLines(a);
    const auto linesB = SplitLines(b);
    if (linesA.size() != linesB.size()) {
        return false;
    }
    constexpr std::string_view kPrefix = "  \"pixelHash\": ";
    for (size_t i = 0; i < linesA.size(); ++i) {
        if (linesA[i].starts_with(kPrefix) && linesB[i].starts_with(kPrefix)) {
            continue;
        }
        if (linesA[i] != linesB[i]) {
            return false;
        }
    }
    return true;
}

}
