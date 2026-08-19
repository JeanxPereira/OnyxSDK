#include <Onyx/Cli/Commands.h>

#include <Onyx/Domain/Entry.h>
#include <Onyx/Modules/DecoderRegistry.h>
#include <Onyx/Modules/Probe.h>
#include <Onyx/Services/Diagnostics.h>
#include <Onyx/Services/Jobs.h>
#include <Onyx/Types/TypeCatalog.h>
#include <Onyx/Vfs/IFile.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>       // SEEK_SET
#include <exception>    // std::exception, contains module IFile calls in ExtractEntries
#include <filesystem>
#include <fstream>
#include <memory>
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
            << "  " << e.source.size << " bytes";
        if ((static_cast<uint8_t>(e.flags) & static_cast<uint8_t>(Domain::NodeFlags::Failed)) != 0) out << " [FAILED]";
        out << "\n";
        PrintTree(out, e.children, cat, depth + 1);
    }
}

void WriteEntryJson(std::ostream& out, const Domain::AssetEntry& e, Types::TypeCatalog& cat) {
    out << "{\"name\":\"" << JsonEscape(e.name) << "\","
        << "\"type\":\"" << JsonEscape(cat.KeyOf(e.typeId)) << "\","
        << "\"size\":" << e.source.size << ","
        << "\"failed\":" << ((static_cast<uint8_t>(e.flags) & static_cast<uint8_t>(Domain::NodeFlags::Failed)) != 0 ? "true" : "false") << ","
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

// Windows reserves these device names as path components regardless of
// case, and regardless of any extension appended to them -- "NUL.txt"
// still resolves to the NUL device, not a file named "NUL.txt". Matched
// against the portion of the name before the first '.'.
bool IsWindowsReservedName(std::string_view name) {
    std::string stem(name.substr(0, name.find('.')));
    for (char& c : stem) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    static constexpr std::array<std::string_view, 22> kReserved = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
    };
    for (std::string_view r : kReserved) {
        if (stem == r) return true;
    }
    return false;
}

// Entry names come from attacker-controlled container bytes, and get joined
// straight to outDir. A name must be a single plain path component -- empty,
// ".", "..", or containing a separator ('/' or '\\') or a drive-letter colon
// (Windows) would let a hostile container write (or overwrite) files outside
// outDir. A Windows reserved device name (NUL, CON, COM1, ... with or
// without an extension) is also rejected: writing to one doesn't create a
// file at all, it opens the device.
bool IsSafeEntryName(std::string_view name) {
    if (name.empty() || name == "." || name == "..") return false;
    for (char c : name) {
        if (c == '/' || c == '\\' || c == ':') return false;
    }
    if (IsWindowsReservedName(name)) return false;
    return true;
}

// Writes every non-Failed leaf entry's payload bytes to outDir/<entry-name>
// -- or, for an entry whose bytes come from a mounted inner file
// (fileIndex != 0), to outDir/<fileIndex>/<entry-name>. A "leaf" is an
// entry with no children -- container/branch nodes carry no payload of
// their own to copy. Failed entries are skipped: their declared range is
// known-bad (that is exactly why the parser flagged them).
//
// `fileTable` is the owning Document's file table (Task 7): slot 0 is
// always the root container file; slot 1+ are inner files a mount-aware
// module opened while parsing. Each entry's payload is read from
// fileTable[e.source.fileIndex] -- an out-of-range index is a salvage
// failure for that one entry (an error line, then keep going), never a
// reason to abort the whole extract.
//
// The per-fileIndex subdirectory (Task 8) exists because a mount can hand
// back several inner files whose entries reuse the same name (e.g. two
// inner containers inside an obxpak, each with a "shared.txt"): writing
// both flat into outDir would let the second overwrite the first. A plain
// single-file container (fileIndex 0 on every entry, e.g. flat .obx) is
// unaffected -- its entries still land directly in outDir, exactly as
// before this scoping was added.
void ExtractEntries(std::ostream& out, const std::vector<Domain::AssetEntry>& entries,
                     const std::filesystem::path& outDir,
                     const std::vector<std::shared_ptr<Vfs::IFile>>& fileTable) {
    for (const auto& e : entries) {
        if (!e.children.empty()) {
            ExtractEntries(out, e.children, outDir, fileTable);
            continue;
        }
        if ((static_cast<uint8_t>(e.flags) & static_cast<uint8_t>(Domain::NodeFlags::Failed)) != 0) continue;

        if (!IsSafeEntryName(e.name)) {
            out << "skipped '" << e.name << "': unsafe name\n";
            continue;
        }

        if (e.source.fileIndex >= fileTable.size() || !fileTable[e.source.fileIndex]) {
            out << "error '" << e.name << "': file index " << e.source.fileIndex
                << " out of range\n";
            continue;
        }
        Vfs::IFile& file = *fileTable[e.source.fileIndex];

        // The alloc/Seek/Read/write below runs module-supplied IFile code
        // for fileIndex >= 1 (a mounted inner file) -- exactly the kind of
        // third-party code the module boundary must contain (spec §7.1). A
        // throwing mount/VFS implementation must salvage this one entry,
        // never abort the whole extract.
        try {
            std::vector<uint8_t> buf(e.source.size);
            size_t got = 0;
            if (e.source.size > 0) {
                file.Seek(int64_t(e.source.offset), SEEK_SET);
                got = file.Read(buf.data(), e.source.size);
            }

            const std::filesystem::path targetDir = e.source.fileIndex == 0
                    ? outDir
                    : outDir / std::to_string(e.source.fileIndex);
            if (targetDir != outDir) {
                std::error_code mkec;
                std::filesystem::create_directories(targetDir, mkec);
            }
            const std::filesystem::path outPath = targetDir / e.name;
            {
                std::ofstream ofs(outPath, std::ios::binary);
                if (!buf.empty()) {
                    ofs.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
                }
            }

            if (got != e.source.size) {
                // A short/failed read means the bytes on disk are truncated or
                // garbage -- never leave a zero-padded file behind for the
                // caller to mistake for a real payload.
                std::error_code ec;
                std::filesystem::remove(outPath, ec);
                out << "error '" << e.name << "': short read\n";
                continue;
            }

            out << "extracted " << e.name << " (" << e.source.size << " bytes)\n";
        } catch (const std::exception& ex) {
            out << "error '" << e.name << "': " << ex.what() << "\n";
            continue;
        } catch (...) {
            out << "error '" << e.name << "': unknown exception during read\n";
            continue;
        }
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
    return rank.winner ? kOk : kNoModule;
}

int CmdList(Workspace& ws, const std::filesystem::path& path, bool json, std::ostream& out,
            std::string_view moduleHint) {
    DocumentId id = ws.Open(path, moduleHint);
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
               const std::filesystem::path& outDir, std::ostream& out,
               std::string_view moduleHint) {
    DocumentId id = ws.Open(path, moduleHint);
    if (id == 0) {
        out << "no module accepts " << path.string() << "\n";
        return kNoModule;
    }

    Document* doc = ws.Get(id);

    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);

    if (!doc->fileTable.empty()) {
        ExtractEntries(out, doc->roots, outDir, doc->fileTable);
    }

    std::vector<Diag> diags = doc->diags.Drain();
    PrintDiags(out, diags);

    ws.Close(id);
    return kOk;
}

int CmdDecode(Workspace& ws, const std::filesystem::path& path, std::string_view entryName,
              bool strict, std::ostream& out, std::string_view moduleHint) {
    DocumentId id = ws.Open(path, moduleHint);
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
    bool hadCapability = false;

    // Scene > Image > Text -- mirrors Onyx::App::RouteForType's priority
    // exactly (Include/Onyx/App/ViewerRouting.h/.cpp, spec sec11: CLI and
    // GUI routing must not diverge). Only ONE branch below ever runs per
    // entry, same as the GUI opening exactly one viewer for a double-click.
    //
    // CLI/oracle boundary: this prints a compact TEXT summary only (parts/
    // materials/vertices) -- it deliberately does NOT reuse Tools/
    // OnyxOracle/RenderReport.h's BuildReport(), which is tool-side (linked
    // only into onyx-oracle/onyx_tests, per Tools/OnyxOracle/CMakeLists.txt
    // and Tests/CMakeLists.txt) and produces a byte-stable JSON report
    // keyed to GL/Vulkan pixel-parity testing, a concern this generic,
    // Onyx_Core-only CLI command has no business depending on (Commands.cpp
    // compiles into Onyx_Core, which links no renderer at all -- see
    // Include/Onyx/Cli/Render.h's top comment for the full boundary
    // writeup). `render` (Examples/OnyxCli/Render.cpp) is the one Cli-side
    // surface that actually touches rendering, and it writes its OWN
    // separate minimal report shape for the identical reason.
    if (reg.HasScene(entry->typeId)) {
        hadCapability = true;
        DecodeContext ctx{*doc, *entry, doc->diags, progress};
        auto scene = reg.DecodeScene(ctx);
        if (scene) {
            size_t totalVertices = 0;
            for (const auto& part : scene->meshParts) totalVertices += part.vertices.size();
            out << "scene " << entry->name << " parts=" << scene->meshParts.size()
                << " materials=" << scene->materials.size() << " vertices=" << totalVertices << "\n";
        }
    } else if (reg.HasImage(entry->typeId)) {
        hadCapability = true;
        DecodeContext ctx{*doc, *entry, doc->diags, progress};
        auto img = reg.DecodeImage(ctx);
        if (img) {
            out << "image " << entry->name << " " << img->width << "x" << img->height
                << " (" << img->pixels.size() << " bytes)\n";
        }
    } else if (reg.HasText(entry->typeId)) {
        hadCapability = true;
        DecodeContext ctx{*doc, *entry, doc->diags, progress};
        auto txt = reg.DecodeText(ctx);
        if (txt) {
            out << txt->text << "\n";
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

    // A capability existed for this entry's type, so this is not a usage
    // error even when the decoder itself returned null -- that is a
    // salvage failure the diags above already explain (e.g. a lying
    // declared size). --strict still overrides on an Error diag.
    return strictFail ? kStrictErrors : kOk;
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
    std::string gameHint;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--json") {
            json = true;
        } else if (a == "--strict") {
            strict = true;
        } else if (a == "--game") {
            // Any position after the subcommand; consumes the next argv
            // as the hint. A trailing "--game" with nothing after it is
            // silently ignored (hint stays empty -- ranking applies).
            if (i + 1 < argc) {
                gameHint = argv[++i];
            }
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
            err << "usage: list <file> [--json] [--game <hint>]\n";
            return kUsage;
        }
        return CmdList(ws, args[0], json, out, gameHint);
    }
    if (cmd == "extract") {
        if (args.size() < 2) {
            err << "usage: extract <file> <outDir> [--game <hint>]\n";
            return kUsage;
        }
        return CmdExtract(ws, args[0], args[1], out, gameHint);
    }
    if (cmd == "decode") {
        if (args.size() < 2) {
            err << "usage: decode <file> <entryName> [--strict] [--game <hint>]\n";
            return kUsage;
        }
        return CmdDecode(ws, args[0], args[1], strict, out, gameHint);
    }

    err << "unknown command: " << cmd << "\n";
    return kUsage;
}

} // namespace Onyx::Cli
