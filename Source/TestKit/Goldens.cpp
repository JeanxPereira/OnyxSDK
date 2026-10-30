#include <Onyx/TestKit/Goldens.h>

#include <Onyx/Core/Cli/Json.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

namespace Onyx::TestKit {

namespace {

void AppendEntry(std::string& out, const Domain::AssetEntry& e, const Types::TypeCatalog& catalog, int depth) {
    const std::string pad(static_cast<size_t>(depth) * 2, ' ');
    const std::string inner(static_cast<size_t>(depth + 1) * 2, ' ');

    out += pad + "{\n";
    out += inner + "\"name\": \"" + Cli::JsonEscape(e.name) + "\",\n";
    out += inner + "\"key\": \"" + Cli::JsonEscape(catalog.Key(e.type)) + "\",\n";
    out += inner + "\"kind\": \"" + std::string(Domain::Name(e.kind)) + "\",\n";
    out += inner + "\"size\": " + std::to_string(e.source.size) + ",\n";
    out += inner + "\"flags\": " + std::to_string(static_cast<unsigned>(e.flags)) + ",\n";

    if (e.children.empty()) {
        out += inner + "\"children\": []\n";
    } else {
        out += inner + "\"children\": [\n";
        for (size_t i = 0; i < e.children.size(); ++i) {
            AppendEntry(out, e.children[i], catalog, depth + 2);
            out += i + 1 < e.children.size() ? ",\n" : "\n";
        }
        out += inner + "]\n";
    }
    out += pad + "}";
}

std::vector<std::string> SplitLines(std::string_view text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        const size_t newline = text.find('\n', start);
        if (newline == std::string_view::npos) {
            lines.emplace_back(text.substr(start));
            break;
        }
        lines.emplace_back(text.substr(start, newline - start));
        start = newline + 1;
    }
    return lines;
}

bool WriteFile(const std::filesystem::path& path, std::string_view text) {
    std::ofstream out(path, std::ios::binary);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    return static_cast<bool>(out);
}

std::string NormalizeLineEndings(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const bool lineEnd = i + 1 == text.size() || text[i + 1] == '\n';
        if (text[i] == '\r' && lineEnd) continue;
        out += text[i];
    }
    return out;
}

bool UpdateRequested() {
#if defined(_WIN32)
    char* buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, "ONYX_UPDATE_GOLDENS") != 0 || !buf) return false;
    const bool on = std::string_view(buf) == "1";
    std::free(buf);
    return on;
#else
    const char* v = std::getenv("ONYX_UPDATE_GOLDENS");
    return v && std::string_view(v) == "1";
#endif
}

}

std::string SnapshotTree(const Modules::Document& doc, const Types::TypeCatalog& catalog) {
    std::string out = "{\n";
    if (doc.roots.empty()) {
        out += "  \"roots\": []\n";
    } else {
        out += "  \"roots\": [\n";
        for (size_t i = 0; i < doc.roots.size(); ++i) {
            AppendEntry(out, doc.roots[i], catalog, 2);
            out += i + 1 < doc.roots.size() ? ",\n" : "\n";
        }
        out += "  ]\n";
    }
    out += "}\n";
    return out;
}

bool CompareTreeGolden(std::string_view snapshot, const std::filesystem::path& goldenFile, std::string& diffOut) {
    std::ifstream in(goldenFile, std::ios::binary);
    const bool exists = static_cast<bool>(in);
    std::string golden;
    if (exists) {
        std::ostringstream buffer;
        buffer << in.rdbuf();
        golden = NormalizeLineEndings(buffer.str());
    }
    const std::string actual = NormalizeLineEndings(snapshot);

    if (exists && golden == actual) {
        diffOut.clear();
        return true;
    }

    std::filesystem::path actualPath = goldenFile;
    actualPath += ".actual";
    std::error_code ec;
    std::filesystem::create_directories(actualPath.parent_path(), ec);
    const std::string wrote = WriteFile(actualPath, actual) ? "wrote " : "could not write ";

    if (!exists) {
        diffOut = "golden file does not exist: " + goldenFile.string() + " (" + wrote + actualPath.string() + ")";
        return false;
    }

    const std::vector<std::string> goldenLines = SplitLines(golden);
    const std::vector<std::string> actualLines = SplitLines(actual);
    const size_t common = std::min(goldenLines.size(), actualLines.size());
    for (size_t i = 0; i < common; ++i) {
        if (goldenLines[i] != actualLines[i]) {
            diffOut = "line " + std::to_string(i + 1) + " differs:\n  golden: " + goldenLines[i] +
                      "\n  actual: " + actualLines[i] + "\n(" + wrote + actualPath.string() + ")";
            return false;
        }
    }
    diffOut = "line count differs: golden has " + std::to_string(goldenLines.size()) + " lines, actual has " +
              std::to_string(actualLines.size()) + " (" + wrote + actualPath.string() + ")";
    return false;
}

bool UpdateGolden(std::string_view snapshot, const std::filesystem::path& goldenFile) {
    if (!UpdateRequested()) return false;
    std::error_code ec;
    std::filesystem::create_directories(goldenFile.parent_path(), ec);
    return WriteFile(goldenFile, snapshot);
}

}
