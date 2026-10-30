#include "Common.h"

#include <Onyx/Core/Cli/Json.h>
#include <Onyx/Core/Cli/Run.h>
#include <Onyx/Core/Modules/DecodeContext.h>
#include <Onyx/Core/Modules/DecoderRegistry.h>
#include <Onyx/Core/Modules/Selection.h>
#include <Onyx/Core/Schema/SchemaJson.h>
#include <Onyx/Core/Schema/SchemaNode.h>

#include <Onyx/Core/Cli/Exporters.h>

#include <charconv>
#include <format>
#include <ostream>

namespace Onyx::Cli::Detail {

namespace {

constexpr size_t kTextPreview = 80;

std::string_view FormatName(Domain::PixelFormat format) {
    switch (format) {
        case Domain::PixelFormat::Rgba8: return "rgba8";
        case Domain::PixelFormat::Bc1: return "bc1";
        case Domain::PixelFormat::Bc3: return "bc3";
        case Domain::PixelFormat::Bc4: return "bc4";
        case Domain::PixelFormat::Bc5: return "bc5";
        case Domain::PixelFormat::Bc7: return "bc7";
        case Domain::PixelFormat::RgbaF16: return "rgbaf16";
    }
    return "unknown";
}

std::optional<Modules::NodePath> ParseNodePath(std::string_view text) {
    Modules::NodePath path;
    while (true) {
        const size_t slash = text.find('/');
        const std::string_view part = text.substr(0, slash);
        uint32_t index = 0;
        const auto [end, ec] = std::from_chars(part.data(), part.data() + part.size(), index);
        if (part.empty() || ec != std::errc() || end != part.data() + part.size()) return std::nullopt;
        path.indices.push_back(index);
        if (slash == std::string_view::npos) return path;
        text.remove_prefix(slash + 1);
    }
}

size_t CountNamed(const std::vector<Domain::AssetEntry>& entries, std::string_view name) {
    size_t count = 0;
    for (const Domain::AssetEntry& e : entries) {
        if (e.name == name) ++count;
        count += CountNamed(e.children, name);
    }
    return count;
}

const Domain::AssetEntry* ResolveTarget(const Modules::Document& doc, std::string_view target, std::ostream& err) {
    if (target.starts_with('@')) {
        const auto path = ParseNodePath(target.substr(1));
        if (!path) {
            err << "invalid node path: " << target << "\n";
            return nullptr;
        }
        return Modules::Resolve(doc, *path);
    }

    const auto path = Modules::FindByName(doc, target);
    if (!path) return nullptr;
    if (const size_t matches = CountNamed(doc.roots, target); matches > 1) {
        err << "note: " << matches << " entries are named '" << target << "'; using the first\n";
    }
    return Modules::Resolve(doc, *path);
}

struct Decoding {
    Modules::Workspace& ws;
    Modules::Document& doc;
    const Domain::AssetEntry& entry;
    Services::Progress progress;

    Vfs::Reader Reader() const {
        return Vfs::Reader(FileOf(doc, entry), entry.source.offset, entry.source.size);
    }

    template <class Fn>
    auto Run(Fn&& decode) {
        Modules::DecodeContext ctx{&doc, entry, Reader(), doc.diags, progress, ws.Settings(), doc.moduleState};
        return decode(ctx);
    }
};

void Summarise(Decoding& d, const Modules::Capabilities& caps, std::ostream& out) {
    const Modules::DecoderRegistry& decoders = d.ws.Decoders();
    const std::string& name = d.entry.name;

    if (caps.scene) {
        if (auto scene = d.Run([&](auto& ctx) { return decoders.DecodeScene(ctx); })) {
            size_t vertices = 0, indices = 0;
            for (const Domain::MeshPart& part : scene->parts) {
                vertices += part.positions.size();
                indices += part.indices.size();
            }
            out << "scene " << name << " parts=" << scene->parts.size() << " materials=" << scene->materials.size()
                << " vertices=" << vertices << " triangles=" << indices / 3 << "\n";
        }
    }
    if (caps.image) {
        if (auto image = d.Run([&](auto& ctx) { return decoders.DecodeImage(ctx); })) {
            out << "image " << name << " " << image->width << "x" << image->height << " " << FormatName(image->format)
                << "\n";
        }
    }
    if (caps.text) {
        if (auto text = d.Run([&](auto& ctx) { return decoders.DecodeText(ctx); })) {
            out << "text " << name << ": " << text->text.substr(0, kTextPreview) << "\n";
        }
    }
    if (caps.audio) {
        if (auto audio = d.Run([&](auto& ctx) { return decoders.DecodeAudio(ctx); })) {
            const double frames = audio->channels ? static_cast<double>(audio->samples.size()) / audio->channels : 0.0;
            const double seconds = audio->sampleRate ? frames / audio->sampleRate : 0.0;
            out << "audio " << name << " rate=" << audio->sampleRate << " channels=" << audio->channels
                << " seconds=" << std::format("{:.2f}", seconds) << "\n";
        }
    }
    if (caps.table) {
        if (auto table = d.Run([&](auto& ctx) { return decoders.DecodeTable(ctx); })) {
            out << "table " << name << " columns=" << table->columns.size() << " rows=" << table->rows.size() << "\n";
        }
    }
    if (caps.schema) {
        if (auto schema = decoders.SchemaOf(d.entry.type)) {
            Vfs::Reader reader = d.Reader();
            auto node = Schema::ReadSchema(*schema, reader, 0);
            if (node.ok()) {
                out << "schema " << name << ": " << Schema::ToJson(node.value()) << "\n";
            } else {
                d.doc.diags.Report({Services::Severity::Error, node.error().code, node.error().message, std::nullopt});
            }
        }
    }
    if (!caps.any.empty()) {
        out << "any " << name << ":";
        for (const std::string& capability : caps.any) out << " " << capability;
        out << "\n";
    }
}

// Decodes the kind the exporter consumes and hands it over. Returns the exit code.
int Export(Decoding& d, const Exporter& exporter, const std::filesystem::path& to, std::string_view format,
           std::ostream& out) {
    const Modules::DecoderRegistry& decoders = d.ws.Decoders();
    Result<void> result = Result<void>::Ok();
    bool decoded = false;

    switch (exporter.kind) {
        case Domain::MediaKind::Mesh:
            if (auto scene = d.Run([&](auto& ctx) { return decoders.DecodeScene(ctx); })) {
                decoded = true;
                result = exporter.write(scene.get(), to);
            }
            break;
        case Domain::MediaKind::Image:
            if (auto image = d.Run([&](auto& ctx) { return decoders.DecodeImage(ctx); })) {
                decoded = true;
                result = exporter.write(&*image, to);
            }
            break;
        case Domain::MediaKind::Audio:
            if (auto audio = d.Run([&](auto& ctx) { return decoders.DecodeAudio(ctx); })) {
                decoded = true;
                result = exporter.write(&*audio, to);
            }
            break;
        default: break;
    }

    if (!decoded) {
        out << "nothing to export: '" << d.entry.name << "' did not decode\n";
        return kPartial;
    }
    if (!result.ok()) {
        out << "export failed: " << result.error().message << "\n";
        return kUsage;
    }
    out << "exported " << d.entry.name << " to " << to.string() << " (" << format << ")\n";
    return kOk;
}

bool KindMatches(Domain::MediaKind kind, const Modules::Capabilities& caps) {
    switch (kind) {
        case Domain::MediaKind::Mesh: return caps.scene;
        case Domain::MediaKind::Image: return caps.image;
        case Domain::MediaKind::Audio: return caps.audio;
        default: return false;
    }
}

}

Verb DecodeVerb(const Exporters* exporters) {
    constexpr std::string_view kUsageText =
        "<file> <entry|@path> [--strict] [--game <hint>] [--to <format> --out <path>]";
    return Verb{
        "decode", std::string(kUsageText),
        [exporters, kUsageText](Modules::Workspace& ws, std::span<const std::string_view> argv, std::ostream& out,
                                std::ostream& err) {
            const Args args = ParseArgs(argv);
            if (args.positional.size() < 2) {
                err << "usage: decode " << kUsageText << '\n';
                return kUsage;
            }

            const std::filesystem::path path(args.positional[0]);
            const std::string_view target = args.positional[1];
            OpenDocument opened(ws, path, args.game);
            if (!OpenOrReport(opened, path, out)) return kNoModule;
            Modules::Document& doc = opened.doc();

            const Domain::AssetEntry* entry = ResolveTarget(doc, target, err);
            if (!entry) {
                out << "unknown entry: " << target << '\n';
                PrintDiags(doc.diags, out);
                return kUsage;
            }

            const Modules::Capabilities caps = ws.Decoders().Of(entry->type);
            if (caps.Empty()) {
                out << "no decoder for entry: " << target << '\n';
                PrintDiags(doc.diags, out);
                return kUsage;
            }

            const Exporter* exporter = nullptr;
            if (!args.to.empty()) {
                exporter = exporters ? exporters->Find(args.to) : nullptr;
                if (!exporter) {
                    out << "unknown export format: " << args.to << '\n';
                    return kUsage;
                }
                if (!KindMatches(exporter->kind, caps)) {
                    out << "cannot decode --to " << args.to << ": entry '" << entry->name
                        << "' has no matching decode capability\n";
                    return kUsage;
                }
                if (args.out.empty()) {
                    err << "decode --to " << args.to << " needs --out <path>\n";
                    return kUsage;
                }
            }

            Decoding decoding{ws, doc, *entry, {}};
            int rc = kOk;
            if (exporter) {
                rc = Export(decoding, *exporter, std::filesystem::path(args.out), args.to, out);
            } else {
                Summarise(decoding, caps, out);
            }

            PrintDiags(doc.diags, out);
            if (rc == kOk && args.strict && doc.diags.HasErrors()) return kStrictErrors;
            return rc;
        }};
}

}
