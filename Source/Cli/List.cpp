#include "Common.h"

#include <Onyx/Core/Cli/Json.h>
#include <Onyx/Core/Cli/Run.h>
#include <Onyx/Core/Services/Paths.h>

#include <ostream>

namespace Onyx::Cli::Detail {

namespace {

void PrintTree(std::ostream& out, const std::vector<Domain::AssetEntry>& entries, const Types::TypeCatalog& catalog,
               int depth) {
    for (const Domain::AssetEntry& e : entries) {
        out << std::string(static_cast<size_t>(depth) * 2, ' ') << e.name << "  " << catalog.Key(e.type) << "  "
            << e.source.size << " bytes";
        if (IsFailed(e)) out << " [FAILED]";
        out << "\n";
        PrintTree(out, e.children, catalog, depth + 1);
    }
}

void WriteEntryJson(std::ostream& out, const Domain::AssetEntry& e, const Types::TypeCatalog& catalog) {
    out << "{\"name\":\"" << JsonEscape(e.name) << "\",\"type\":\"" << JsonEscape(catalog.Key(e.type))
        << "\",\"size\":" << e.source.size << ",\"failed\":" << (IsFailed(e) ? "true" : "false")
        << ",\"children\":[";
    for (size_t i = 0; i < e.children.size(); ++i) {
        if (i) out << ",";
        WriteEntryJson(out, e.children[i], catalog);
    }
    out << "]}";
}

void WriteJson(std::ostream& out, const std::filesystem::path& path, const Modules::Document& doc,
               const Types::TypeCatalog& catalog) {
    out << "{\"path\":\"" << JsonEscape(path.string()) << "\",\"module\":\""
        << JsonEscape(doc.module ? doc.module->Info().id : std::string()) << "\",\"entries\":[";
    for (size_t i = 0; i < doc.roots.size(); ++i) {
        if (i) out << ",";
        WriteEntryJson(out, doc.roots[i], catalog);
    }
    out << "],\"diags\":[";
    const std::vector<Services::Diag> diags = doc.diags.Snapshot();
    for (size_t i = 0; i < diags.size(); ++i) {
        if (i) out << ",";
        out << "{\"severity\":\"" << SeverityLabel(diags[i].severity) << "\",\"code\":\"" << JsonEscape(diags[i].code)
            << "\",\"message\":\"" << JsonEscape(diags[i].message) << "\"}";
    }
    out << "]}\n";
}

}

Verb ListVerb() {
    return Verb{
        "list", "<file> [--json] [--game <hint>]",
        [](Modules::Workspace& ws, const VerbTable&, std::span<const std::string_view> argv, std::ostream& out, std::ostream& err) {
            Args args;
            if (!ParseArgs(argv, args) || args.positional.empty()) {
                err << "usage: list <file> [--json] [--game <hint>]\n";
                return kUsage;
            }

            const std::filesystem::path path = Services::Paths::FromUtf8(args.positional[0]);
            Modules::ScopedDocument opened(ws, path, args.game);
            if (!OpenOrReport(opened, path, out)) return kNoModule;

            if (args.json) {
                WriteJson(out, path, opened.doc(), ws.Catalog());
            } else {
                PrintTree(out, opened.doc().roots, ws.Catalog(), 0);
                PrintDiags(opened.doc().diags, out);
            }
            return kOk;
        }};
}

}
