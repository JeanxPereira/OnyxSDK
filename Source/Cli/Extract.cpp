#include "Common.h"

#include <Onyx/Core/Cli/Json.h>
#include <Onyx/Core/Cli/Run.h>
#include <Onyx/Core/Services/Paths.h>
#include <Onyx/Core/Vfs/SliceFile.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <fstream>
#include <ostream>
#include <system_error>

namespace Onyx::Cli::Detail {

namespace {

constexpr size_t kChunk = 64 * 1024;

bool IsWindowsReservedName(std::string_view name) {
    std::string stem(name.substr(0, name.find('.')));
    for (char& c : stem) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    static constexpr std::array<std::string_view, 22> kReserved = {
        "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9", "COM8", "COM9",
    };
    return std::find(kReserved.begin(), kReserved.end(), stem) != kReserved.end();
}

// Names come from the container, so they must be one plain path component that resolves inside
// the output directory.
bool IsSafeEntryName(std::string_view name, const std::filesystem::path& dir) {
    if (name.empty() || name == "." || name == "..") return false;
    if (name.find_first_of("/\\:") != std::string_view::npos) return false;
    if (IsWindowsReservedName(name)) return false;
    return Services::Paths::IsInside(dir, dir / std::filesystem::path(std::string(name)));
}

enum class Outcome { Written, Truncated, Failed };

// Streams the entry through a SliceFile so the declared size never drives an allocation.
Outcome WriteEntry(const std::shared_ptr<const Vfs::IFile>& file, const Domain::ByteRange& range,
                   const std::filesystem::path& target, uint64_t& written) {
    const Vfs::SliceFile slice(file, range.offset, range.size);
    std::ofstream ofs(target, std::ios::binary);
    if (!ofs) return Outcome::Failed;

    std::vector<std::byte> chunk(kChunk);
    written = 0;
    while (written < slice.Size()) {
        const size_t got = slice.ReadAt(written, chunk);
        if (got == 0) break;
        ofs.write(reinterpret_cast<const char*>(chunk.data()), static_cast<std::streamsize>(got));
        written += got;
    }
    ofs.flush();
    if (!ofs) return Outcome::Failed;
    return written == range.size ? Outcome::Written : Outcome::Truncated;
}

void ExtractEntries(std::ostream& out, const Modules::Document& doc, const std::vector<Domain::AssetEntry>& entries,
                    const std::filesystem::path& outDir, size_t& failures) {
    for (const Domain::AssetEntry& e : entries) {
        if (!e.children.empty()) {
            ExtractEntries(out, doc, e.children, outDir, failures);
            continue;
        }
        if (IsFailed(e)) continue;

        if (!IsSafeEntryName(e.name, outDir)) {
            out << "skipped '" << e.name << "': unsafe name\n";
            ++failures;
            continue;
        }

        const std::shared_ptr<const Vfs::IFile> file = FileOf(doc, e);
        if (!file) {
            out << "error '" << e.name << "': file index " << e.source.fileIndex << " out of range\n";
            ++failures;
            continue;
        }

        try {
            const std::filesystem::path targetDir =
                e.source.fileIndex == 0 ? outDir : outDir / std::to_string(e.source.fileIndex);
            std::error_code ec;
            std::filesystem::create_directories(targetDir, ec);

            uint64_t written = 0;
            switch (WriteEntry(file, e.source, targetDir / e.name, written)) {
                case Outcome::Written:
                    out << "extracted " << e.name << " (" << written << " bytes)\n";
                    break;
                case Outcome::Truncated:
                    out << "error '" << e.name << "': truncated, wrote " << written << " of " << e.source.size
                        << " bytes\n";
                    ++failures;
                    break;
                case Outcome::Failed:
                    out << "error '" << e.name << "': cannot write output\n";
                    ++failures;
                    break;
            }
        } catch (const std::exception& ex) {
            out << "error '" << e.name << "': " << ex.what() << "\n";
            ++failures;
        } catch (...) {
            out << "error '" << e.name << "': unknown exception during read\n";
            ++failures;
        }
    }
}

}

Verb ExtractVerb() {
    return Verb{
        "extract", "<file> <dir> [--game <hint>]",
        [](Modules::Workspace& ws, std::span<const std::string_view> argv, std::ostream& out, std::ostream& err) {
            const Args args = ParseArgs(argv);
            if (args.positional.size() < 2) {
                err << "usage: extract <file> <dir> [--game <hint>]\n";
                return kUsage;
            }

            const std::filesystem::path path(args.positional[0]);
            const std::filesystem::path outDir(args.positional[1]);
            OpenDocument opened(ws, path, args.game);
            if (!OpenOrReport(opened, path, out)) return kNoModule;

            std::error_code ec;
            std::filesystem::create_directories(outDir, ec);
            if (ec) {
                out << "cannot create " << outDir.string() << ": " << ec.message() << "\n";
                return kPartial;
            }

            size_t failures = 0;
            ExtractEntries(out, opened.doc(), opened.doc().roots, outDir, failures);
            PrintDiags(opened.doc().diags, out);
            return failures == 0 ? kOk : kPartial;
        }};
}

}
