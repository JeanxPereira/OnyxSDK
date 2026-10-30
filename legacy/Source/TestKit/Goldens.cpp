#include <Onyx/TestKit/Goldens.h>

#include <Onyx/Domain/Entry.h>
#include <Onyx/Types/TypeCatalog.h>
#include <Onyx/Vfs/IFile.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>       // SEEK_SET
#include <fstream>
#include <sstream>
#include <vector>

namespace Onyx::TestKit {

namespace {

// Self-contained 64-bit FNV-1a (offset basis 14695981039346656037, prime
// 1099511628211) -- TestKit is a lower layer than Tools/OnyxOracle
// (RenderReport.h has its own copy of the identical algorithm), so this is
// reimplemented here rather than pulled from tool code: a public SDK
// header/source must never depend on Tools/.
uint64_t Fnv1a(const void* data, size_t len) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < len; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

// Minimal JSON string escaping: backslash, quote, and control characters --
// same minimal contract Source/Cli/Commands.cpp's JsonEscape and Tools/
// OnyxOracle/RenderReport.cpp's AppendJsonString both already use; not
// shared across translation units (both of those are anonymous-namespace
// private too), so reimplemented here rather than exposed as a dependency.
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

bool IsFailed(const Domain::AssetEntry& e) {
    return (static_cast<uint8_t>(e.flags) & static_cast<uint8_t>(Domain::NodeFlags::Failed)) != 0;
}

// FNV-1a of an entry's declared payload bytes, read through the Document's
// file table (mirrors Source/Cli/Commands.cpp's ExtractEntries read path).
// Returns 0 for an entry with no payload (Empty ByteRange) or a Failed
// entry (its declared range is already known-bad -- reading it would mean
// trusting a range the parser itself flagged as untrustworthy, and for a
// corrupt entry that range can be enormous, e.g. spec §5.4's 0xFFFFFFFF
// widening case -- so Failed entries are never read here, same as
// ExtractEntries skips them for the same reason). A fileIndex out of range,
// a null file table slot, or a short read all degrade to hashing whatever
// was actually read (possibly nothing) rather than throwing.
uint64_t PayloadHash(const Modules::Document& doc, const Domain::AssetEntry& e) {
    if (e.source.Empty() || IsFailed(e)) return 0;
    if (e.source.fileIndex >= doc.fileTable.size() || !doc.fileTable[e.source.fileIndex]) return 0;

    Vfs::IFile& file = *doc.fileTable[e.source.fileIndex];
    std::vector<uint8_t> buf(e.source.size);
    if (!file.Seek(static_cast<int64_t>(e.source.offset), SEEK_SET)) return 0;
    size_t got = file.Read(buf.data(), e.source.size);
    return Fnv1a(buf.data(), got);
}

void AppendEntry(std::string& out, const Modules::Document& doc, const Domain::AssetEntry& e,
                  Types::TypeCatalog& cat, int depth) {
    const std::string pad(static_cast<size_t>(depth) * 2, ' ');
    const std::string padChild((static_cast<size_t>(depth) + 1) * 2, ' ');

    out += pad + "{\n";
    out += padChild + "\"name\": ";
    AppendJsonString(out, e.name);
    out += ",\n";

    out += padChild + "\"key\": ";
    AppendJsonString(out, std::string(cat.KeyOf(e.typeId)));
    out += ",\n";

    out += padChild + "\"size\": " + std::to_string(e.source.size) + ",\n";

    char hashBuf[32];
    std::snprintf(hashBuf, sizeof(hashBuf), "%llu",
                  static_cast<unsigned long long>(PayloadHash(doc, e)));
    out += padChild + "\"hash\": " + hashBuf + ",\n";

    out += padChild + "\"failed\": ";
    out += IsFailed(e) ? "true" : "false";
    out += ",\n";

    if (e.children.empty()) {
        out += padChild + "\"children\": []\n";
    } else {
        out += padChild + "\"children\": [\n";
        for (size_t i = 0; i < e.children.size(); ++i) {
            AppendEntry(out, doc, e.children[i], cat, depth + 2);
            out += (i + 1 < e.children.size()) ? ",\n" : "\n";
        }
        out += padChild + "]\n";
    }

    out += pad + "}";
}

} // namespace

std::string SnapshotTree(const Modules::Document& doc) {
    Types::TypeCatalog& cat = Types::TypeCatalog::Get();

    std::string out;
    out += "{\n";
    if (doc.roots.empty()) {
        out += "  \"roots\": []\n";
    } else {
        out += "  \"roots\": [\n";
        for (size_t i = 0; i < doc.roots.size(); ++i) {
            AppendEntry(out, doc, doc.roots[i], cat, 2);
            out += (i + 1 < doc.roots.size()) ? ",\n" : "\n";
        }
        out += "  ]\n";
    }
    out += "}\n";
    return out;
}

bool CompareTreeGolden(const std::string& snapshot, const std::filesystem::path& goldenFile,
                        std::string& diffOut) {
    std::ifstream in(goldenFile, std::ios::binary);
    std::string golden;
    bool exists = static_cast<bool>(in);
    if (exists) {
        std::ostringstream ss;
        ss << in.rdbuf();
        golden = ss.str();
    }

    if (exists && golden == snapshot) {
        diffOut.clear();
        return true;
    }

    // Write the actual snapshot next to the golden so a developer can
    // `diff goldenFile goldenFile.actual` directly -- same pattern the
    // oracle's own render-corpus comparison relies on (a frozen expectation
    // beside what actually came out).
    std::filesystem::path actualPath = goldenFile;
    actualPath += ".actual";
    {
        std::ofstream out(actualPath, std::ios::binary);
        out << snapshot;
    }

    if (!exists) {
        diffOut = "golden file does not exist: " + goldenFile.string() + " (wrote " +
                  actualPath.string() + ")";
        return false;
    }

    // Line-based first-difference report: split on '\n' (no trailing empty
    // segment past a final '\n', same convention Tools/OnyxOracle/
    // RenderReport.cpp's JsonEqualMaskingPixelHash already established).
    auto splitLines = [](const std::string& s) {
        std::vector<std::string> lines;
        size_t start = 0;
        while (start < s.size()) {
            size_t nl = s.find('\n', start);
            if (nl == std::string::npos) {
                lines.push_back(s.substr(start));
                break;
            }
            lines.push_back(s.substr(start, nl - start));
            start = nl + 1;
        }
        return lines;
    };

    std::vector<std::string> goldenLines = splitLines(golden);
    std::vector<std::string> actualLines = splitLines(snapshot);
    size_t n = std::min(goldenLines.size(), actualLines.size());
    for (size_t i = 0; i < n; ++i) {
        if (goldenLines[i] != actualLines[i]) {
            diffOut = "line " + std::to_string(i + 1) + " differs:\n  golden: " + goldenLines[i] +
                       "\n  actual: " + actualLines[i] + "\n(wrote " + actualPath.string() + ")";
            return false;
        }
    }
    diffOut = "line count differs: golden has " + std::to_string(goldenLines.size()) +
               " lines, actual has " + std::to_string(actualLines.size()) + " lines (wrote " +
               actualPath.string() + ")";
    return false;
}

} // namespace Onyx::TestKit
