#include "RenderReport.h"

#include <charconv>
#include <cstdio>
#include <system_error>

namespace Onyx::OracleTool {

using Onyx::Parsers::BlendMode;
using Onyx::Rendering::RenderBatch;

uint64_t Fnv1a(const void* data, size_t len) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = 14695981039346656037ull; // offset basis
    for (size_t i = 0; i < len; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull; // prime
    }
    return hash;
}

std::string FormatFloat(float v) {
    char buf[64];
    // std::to_chars is locale-independent by the standard's specification
    // (unlike snprintf("%.6f"), which reads LC_NUMERIC and would emit
    // "0,250000" under a comma-decimal locale) -- exactly the failure
    // this report's byte-stability contract exists to prevent.
    auto result = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed, 6);
    if (result.ec != std::errc()) {
        // Practically unreachable at the value ranges this report ever
        // sees (material colors, metallic factors) -- fail safe instead
        // of reading past an unterminated buffer.
        return "0.000000";
    }

    // to_chars renders -0.0f (and any value that rounds to all-zero digits
    // at 6 decimals) as "-0.000000", same as printf would. Normalize that
    // to "0.000000" so the report never encodes sign-of-zero, which is not
    // itself deterministic across platforms/compilers for float arithmetic
    // that lands on zero.
    if (buf[0] == '-') {
        bool allZero = true;
        for (const char* p = buf + 1; p != result.ptr; ++p) {
            if (*p != '0' && *p != '.') { allZero = false; break; }
        }
        if (allZero) return std::string(buf + 1, result.ptr);
    }
    return std::string(buf, result.ptr);
}

namespace {

const char* BlendModeName(BlendMode mode) {
    switch (mode) {
        case BlendMode::Normal:      return "Normal";
        case BlendMode::Additive:    return "Additive";
        case BlendMode::Subtractive: return "Subtractive";
        case BlendMode::EnvMap:      return "EnvMap";
    }
    return "Normal"; // unreachable for a valid BlendMode value
}

// Minimal JSON string escaping: backslash, quote, and control characters.
// Corpus scene/batch names are ASCII identifiers in practice, but this
// keeps the report well-formed even if that ever changes.
void AppendJsonString(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

void AppendBool(std::string& out, bool v) {
    out += v ? "true" : "false";
}

// Six GL role slots today: texture0, texture1, texNormal, texAO, texGloss,
// texScatter. M4 widens the role array (see the RenderBatch comment in
// SceneRenderer.h) -- extend this list alongside it when that lands.
int CountBoundRoleTextures(const RenderBatch& b) {
    const GLuint slots[] = {b.texture0, b.texture1, b.texNormal,
                             b.texAO,    b.texGloss, b.texScatter};
    int count = 0;
    for (GLuint id : slots) {
        if (id != 0) ++count;
    }
    return count;
}

} // namespace

std::string BuildReport(const std::string& sceneName, int w, int h,
                        uint64_t pixelHash,
                        const std::vector<RenderBatch>& batches,
                        const std::vector<size_t>& paletteJointCounts) {
    std::string out;
    out += "{\n";

    out += "  \"scene\": ";
    AppendJsonString(out, sceneName);
    out += ",\n";

    out += "  \"width\": " + std::to_string(w) + ",\n";
    out += "  \"height\": " + std::to_string(h) + ",\n";

    char hashBuf[32];
    std::snprintf(hashBuf, sizeof(hashBuf), "%llu", static_cast<unsigned long long>(pixelHash));
    out += "  \"pixelHash\": ";
    out += hashBuf;
    out += ",\n";

    if (batches.empty()) {
        out += "  \"batches\": []\n";
    } else {
        out += "  \"batches\": [\n";
        for (size_t i = 0; i < batches.size(); ++i) {
            const RenderBatch& b = batches[i];
            size_t paletteJointCount = (i < paletteJointCounts.size()) ? paletteJointCounts[i] : 0;

            out += "    {\n";

            out += "      \"name\": ";
            AppendJsonString(out, b.name);
            out += ",\n";

            out += "      \"vertexCount\": " + std::to_string(b.vertexCount) + ",\n";
            out += "      \"triangleCount\": " + std::to_string(b.triangleCount) + ",\n";
            out += std::string("      \"blendMode\": \"") + BlendModeName(b.blendMode) + "\",\n";

            out += "      \"hasTexture\": ";
            AppendBool(out, b.hasTexture);
            out += ",\n";

            out += "      \"hasEnvmap\": ";
            AppendBool(out, b.hasEnvmap);
            out += ",\n";

            out += "      \"hasSkeleton\": ";
            AppendBool(out, b.hasSkeleton);
            out += ",\n";

            out += "      \"metallic\": " + FormatFloat(b.metallic) + ",\n";

            out += "      \"materialColor\": [";
            for (int c = 0; c < 4; ++c) {
                out += FormatFloat(b.materialColor[c]);
                if (c != 3) out += ", ";
            }
            out += "],\n";

            out += "      \"roleTexturesBound\": " + std::to_string(CountBoundRoleTextures(b)) + ",\n";
            out += "      \"paletteJointCount\": " + std::to_string(paletteJointCount) + "\n";

            out += "    }";
            out += (i + 1 < batches.size()) ? ",\n" : "\n";
        }
        out += "  ]\n";
    }

    out += "}\n";
    return out;
}

} // namespace Onyx::OracleTool
