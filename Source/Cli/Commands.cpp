#include <Onyx/Cli/Commands.h>

#include <Onyx/Domain/Entry.h>
#include <Onyx/Modules/DecoderRegistry.h>
#include <Onyx/Modules/Probe.h>
#include <Onyx/Services/Diagnostics.h>
#include <Onyx/Services/Jobs.h>
#include <Onyx/Types/TypeCatalog.h>
#include <Onyx/Vfs/IFile.h>

#include <cstdint>
#include <cstdio>       // SEEK_SET
#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace Onyx::Cli {

using Modules::DecodeContext;
using Modules::DecoderRegistry;
using Modules::Document;
using Modules::DocumentId;
using Modules::Workspace;
using Services::Diag;
using Services::DiagSink;
using Services::Progress;
using Services::Severity;

namespace {

const char* SeverityLabel(Severity s) {
    switch (s) {
        case Severity::Error:   return "Error";
        case Severity::Warning: return "Warning";
        default:                return "Info";
    }
}

// Minimal JSON string escaping: backslash, quote, and control characters.
// No Unicode normalization -- the SDK carries no JSON dependency, this is
// just enough to keep the hand-emitted object well-formed.
std::string JsonEscape(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += char(c);
                }
                break;
        }
    }
    return out;
}

void PrintDiags(std::ostream& out, const std::vector<Diag>& diags) {
    for (const auto& d : diags) {
        out << "[" << SeverityLabel(d.severity) << "] " << d.code << ": " << d.message << "\n";
    }
}

bool AnyError(const std::vector<Diag>& diags) {
    for (const auto& d : diags) {
        if (d.severity == Severity::Error) return true;
    }
    return false;
}

const Domain::AssetEntry* FindEntryByName(const std::vector<Domain::AssetEntry>& entries,
                                           std::string_view name) {
    for (const auto& e : entries) {
        if (e.name == name) return &e;
        if (const auto* found = FindEntryByName(e.children, name)) return found;
    }
    return nullptr;
}

void PrintTree(std::ostream& out, const std::vector<Domain::AssetEntry>& entries,
               Types::TypeCatalog& cat, int depth) {
    for (const auto& e : entries) {
        out << std::string(size_t(depth) * 2, ' ') << e.name << "  " << cat.KeyOf(e.typeId)
            << "  " << e.size << " bytes";
        if (e.flags == Domain::NodeFlags::Failed) out << " [FAILED]";
        out << "\n";
        PrintTree(out, e.children, cat, depth + 1);
    }
}

void WriteEntryJson(std::ostream& out, const Domain::AssetEntry& e, Types::TypeCatalog& cat) {
    out << "{\"name\":\"" << JsonEscape(e.name) << "\","
        << "\"type\":\"" << JsonEscape(cat.KeyOf(e.typeId)) << "\","
        << "\"size\":" << e.size << ","
        << "\"failed\":" << (e.flags == Domain::NodeFlags::Failed ? "true" : "false") << ","
        << "\"children\":[";
    for (size_t i = 0; i < e.children.size(); ++i) {
        if (i) out << ",";
        WriteEntryJson(out, e.children[i], cat);
    }
    out << "]}";
}

void WriteDiagJson(std::ostream& out, const Diag& d) {
    out << "{\"severity\":\"" << SeverityLabel(d.severity) << "\","
        << "\"code\":\"" << JsonEscape(d.code) << "\","
        << "\"message\":\"" << JsonEscape(d.message) << "\"}";
}

// Writes every non-Failed leaf entry's payload bytes to outDir/<entry-name>.
// A "leaf" is an entry with no children -- container/branch nodes carry no
// payload of their own to copy. Failed entries are skipped: their declared
// range is known-bad (that is exactly why the parser flagged them).
void ExtractEntries(std::ostream& out, const std::vector<Domain::AssetEntry>& entries,
                     const std::filesystem::path& outDir, Vfs::IFile& file) {
    for (const auto& e : entries) {
        if (!e.children.empty()) {
            ExtractEntries(out, e.children, outDir, file);
            continue;
        }
        if (e.flags == Domain::NodeFlags::Failed) continue;

        std::vector<uint8_t> buf(e.size);
        if (e.size > 0) {
            file.Seek(int64_t(e.offset), SEEK_SET);
            file.Read(buf.data(), e.size);
        }

        std::ofstream ofs(outDir / e.name, std::ios::binary);
        if (!buf.empty()) {
            ofs.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
        }
        out << "extracted " << e.name << " (" << e.size << " bytes)\n";
    }
}

} // namespace

int CmdProbe(Workspace& ws, const std::filesystem::path& path, std::ostream& out) {
    Modules::ProbeRanking rank = ws.Probe(path);

    out << "module\tconfidence\treason\n";
    for (const auto& row : rank.rows) {
        out << row.module->Info().id << "\t" << row.result.confidence << "\t"
            << row.result.reason << "\n";
    }
    out << "winner: " << (rank.winner ? rank.winner->Info().id : "none") << "\n";
    return kOk;
}

int CmdList(Workspace& ws, const std::filesystem::path& path, bool json, std::ostream& out) {
    DocumentId id = ws.Open(path);
    if (id == 0) {
        out << "no module accepts " << path.string() << "\n";
        return kNoModule;
    }

    Document* doc = ws.Get(id);
    std::vector<Diag> diags = doc->diags.Drain();
    Types::TypeCatalog& cat = ws.Catalog();
    const std::string moduleId = doc->module ? doc->module->Info().id : std::string();

    if (json) {
        out << "{\"path\":\"" << JsonEscape(path.string()) << "\","
            << "\"module\":\"" << JsonEscape(moduleId) << "\","
            << "\"entries\":[";
        for (size_t i = 0; i < doc->roots.size(); ++i) {
            if (i) out << ",";
            WriteEntryJson(out, doc->roots[i], cat);
        }
        out << "],\"diags\":[";
        for (size_t i = 0; i < diags.size(); ++i) {
            if (i) out << ",";
            WriteDiagJson(out, diags[i]);
        }
        out << "]}\n";
    } else {
        PrintTree(out, doc->roots, cat, 0);
        PrintDiags(out, diags);
    }

    ws.Close(id);
    return kOk;
}

int CmdExtract(Workspace& ws, const std::filesystem::path& path,
               const std::filesystem::path& outDir, std::ostream& out) {
    DocumentId id = ws.Open(path);
    if (id == 0) {
        out << "no module accepts " << path.string() << "\n";
        return kNoModule;
    }

    Document* doc = ws.Get(id);

    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    if (doc->file) {
        ExtractEntries(out, doc->roots, outDir, *doc->file);
    }

    std::vector<Diag> diags = doc->diags.Drain();
    PrintDiags(out, diags);

    ws.Close(id);
    return kOk;
}

int CmdDecode(Workspace& ws, const std::filesystem::path& path, std::string_view entryName,
              bool strict, std::ostream& out) {
    DocumentId id = ws.Open(path);
    if (id == 0) {
        out << "no module accepts " << path.string() << "\n";
        return kNoModule;
    }

    Document* doc = ws.Get(id);
    const Domain::AssetEntry* entry = FindEntryByName(doc->roots, entryName);
    if (!entry) {
        out << "unknown entry: " << entryName << "\n";
        std::vector<Diag> diags = doc->diags.Drain();
        PrintDiags(out, diags);
        ws.Close(id);
        return kUsage;
    }

    DecoderRegistry& reg = ws.Decoders();
    Progress progress;
    bool decoded = false;
    bool hadCapability = false;

    if (reg.HasImage(entry->typeId)) {
        hadCapability = true;
        DecodeContext ctx{*doc, *entry, doc->diags, progress};
        auto img = reg.DecodeImage(ctx);
        if (img) {
            out << "image " << entry->name << " " << img->width << "x" << img->height
                << " (" << img->pixels.size() << " bytes)\n";
            decoded = true;
        }
    } else if (reg.HasText(entry->typeId)) {
        hadCapability = true;
        DecodeContext ctx{*doc, *entry, doc->diags, progress};
        auto txt = reg.DecodeText(ctx);
        if (txt) {
            out << txt->text << "\n";
            decoded = true;
        }
    }

    if (!hadCapability) {
        out << "no decoder for entry: " << entryName << "\n";
        std::vector<Diag> diags = doc->diags.Drain();
        PrintDiags(out, diags);
        ws.Close(id);
        return kUsage;
    }

    std::vector<Diag> diags = doc->diags.Drain();
    PrintDiags(out, diags);
    const bool strictFail = strict && AnyError(diags);

    ws.Close(id);

    if (strictFail) return kStrictErrors;
    return decoded ? kOk : kUsage;
}

int Run(Workspace& ws, int argc, char** argv, std::ostream& out, std::ostream& err) {
    if (argc < 2) {
        err << "usage: onyxbox-cli <probe|list|extract|decode> <file> [options]\n";
        return kUsage;
    }

    const std::string cmd = argv[1];
    std::vector<std::string> args;
    bool json = false;
    bool strict = false;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--json") {
            json = true;
        } else if (a == "--strict") {
            strict = true;
        } else {
            args.push_back(a);
        }
    }

    if (cmd == "probe") {
        if (args.empty()) {
            err << "usage: probe <file>\n";
            return kUsage;
        }
        return CmdProbe(ws, args[0], out);
    }
    if (cmd == "list") {
        if (args.empty()) {
            err << "usage: list <file> [--json]\n";
            return kUsage;
        }
        return CmdList(ws, args[0], json, out);
    }
    if (cmd == "extract") {
        if (args.size() < 2) {
            err << "usage: extract <file> <outDir>\n";
            return kUsage;
        }
        return CmdExtract(ws, args[0], args[1], out);
    }
    if (cmd == "decode") {
        if (args.size() < 2) {
            err << "usage: decode <file> <entryName> [--strict]\n";
            return kUsage;
        }
        return CmdDecode(ws, args[0], args[1], strict, out);
    }

    err << "unknown command: " << cmd << "\n";
    return kUsage;
}

} // namespace Onyx::Cli
