#include <Prism/Module.h>

#include <Prism/Format.h>
#include <Prism/Palette.h>

#include <Onyx/Core/Modules/DecoderRegistry.h>
#include <Onyx/Core/Modules/Document.h>
#include <Onyx/Core/Schema/Schema.h>
#include <Onyx/Core/Services/SettingsSchema.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <memory>
#include <optional>
#include <string>

namespace Prism {

using namespace Onyx;

namespace {

constexpr std::string_view kPaletteCapability = "prism.palette";

void Report(Services::DiagSink& diags, Services::Severity severity, std::string code, std::string message) {
    diags.Report(Services::Diag{severity, std::move(code), std::move(message), std::nullopt});
}

bool StartsWith(std::span<const std::byte> bytes, std::string_view magic) {
    return bytes.size() >= magic.size() && std::memcmp(bytes.data(), magic.data(), magic.size()) == 0;
}

struct SectionHeader {
    uint8_t kind = 0;
    std::string name;
    uint32_t offset = 0;
    uint32_t size = 0;
};

std::optional<Palette> ReadPalette(Vfs::Reader& reader) {
    if (reader.Size() % 4 != 0) return std::nullopt;
    Palette palette;
    palette.colors.reserve(static_cast<size_t>(reader.Size() / 4));
    while (!reader.Eof()) {
        std::array<std::byte, 4> rgba{};
        if (reader.Read(rgba) != rgba.size()) return std::nullopt;
        palette.colors.emplace_back(static_cast<uint8_t>(rgba[0]), static_cast<uint8_t>(rgba[1]),
                                    static_cast<uint8_t>(rgba[2]), static_cast<uint8_t>(rgba[3]));
    }
    return palette;
}

std::optional<Domain::Table> DecodePaletteTable(Modules::DecodeContext& ctx) {
    const std::optional<Palette> palette = ReadPalette(ctx.reader);
    if (!palette) {
        Report(ctx.diags, Services::Severity::Error, "prism.palette.size",
               ctx.entry.name + ": palette size is not a multiple of 4");
        return std::nullopt;
    }
    Domain::Table table;
    table.columns = {"index", "r", "g", "b", "a"};
    for (size_t i = 0; i < palette->colors.size(); ++i) {
        const glm::u8vec4& c = palette->colors[i];
        table.rows.push_back({std::to_string(i), std::to_string(c.r), std::to_string(c.g), std::to_string(c.b),
                              std::to_string(c.a)});
    }
    return table;
}

std::shared_ptr<void> DecodePaletteAny(Modules::DecodeContext& ctx) {
    std::optional<Palette> palette = ReadPalette(ctx.reader);
    if (!palette) {
        Report(ctx.diags, Services::Severity::Error, "prism.palette.size",
               ctx.entry.name + ": palette size is not a multiple of 4");
        return nullptr;
    }
    return std::make_shared<Palette>(std::move(*palette));
}

std::optional<Domain::Texture> DecodeImage(Modules::DecodeContext& ctx) {
    const auto palette = std::static_pointer_cast<const Palette>(ctx.moduleState);
    if (!palette) {
        Report(ctx.diags, Services::Severity::Error, "prism.noPalette", ctx.entry.name + ": the file has no palette");
        return std::nullopt;
    }

    Vfs::Reader& reader = ctx.reader;
    const auto width = reader.Read<uint16_t>();
    const auto height = reader.Read<uint16_t>();
    if (!width || !height) {
        Report(ctx.diags, Services::Severity::Error, "prism.image.truncated", ctx.entry.name + ": header truncated");
        return std::nullopt;
    }
    const uint64_t pixels = uint64_t(*width) * uint64_t(*height);
    if (kImageHeaderSize + pixels != reader.Size()) {
        Report(ctx.diags, Services::Severity::Error, "prism.image.size",
               ctx.entry.name + ": declared " + std::to_string(*width) + "x" + std::to_string(*height) +
                   " does not match the payload");
        return std::nullopt;
    }

    std::vector<std::byte> indices(static_cast<size_t>(pixels));
    if (reader.Read(indices) != pixels) {
        Report(ctx.diags, Services::Severity::Error, "prism.image.truncated", ctx.entry.name + ": pixels truncated");
        return std::nullopt;
    }

    const bool transparentZero = ctx.settings.GetBool(kTransparentIndexZeroKey).value_or(false);
    Domain::Texture texture;
    texture.name = ctx.entry.name;
    texture.width = *width;
    texture.height = *height;
    texture.format = Domain::PixelFormat::Rgba8;
    texture.pixels.resize(static_cast<size_t>(pixels) * 4);
    for (size_t i = 0; i < indices.size(); ++i) {
        const auto index = static_cast<size_t>(indices[i]);
        if (index >= palette->colors.size()) {
            Report(ctx.diags, Services::Severity::Error, "prism.image.index",
                   ctx.entry.name + ": index " + std::to_string(index) + " is past the " +
                       std::to_string(palette->colors.size()) + " palette colours");
            return std::nullopt;
        }
        glm::u8vec4 color = palette->colors[index];
        if (transparentZero && index == 0) color = glm::u8vec4(0, 0, 0, 0);
        std::byte* out = texture.pixels.data() + i * 4;
        out[0] = static_cast<std::byte>(color.r);
        out[1] = static_cast<std::byte>(color.g);
        out[2] = static_cast<std::byte>(color.b);
        out[3] = static_cast<std::byte>(color.a);
    }
    return texture;
}

std::optional<Domain::Text> DecodeManifest(Modules::DecodeContext& ctx) {
    const std::vector<std::byte> bytes = ctx.reader.ReadAll();
    Domain::Text text;
    text.text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    text.language = "toml";
    return text;
}

std::optional<Domain::Audio> DecodeClip(Modules::DecodeContext& ctx) {
    Vfs::Reader& reader = ctx.reader;
    const auto rate = reader.Read<uint32_t>();
    const auto channels = reader.Read<uint16_t>();
    const auto frames = reader.Read<uint32_t>();
    if (!rate || !channels || !frames) {
        Report(ctx.diags, Services::Severity::Error, "prism.clip.truncated", ctx.entry.name + ": header truncated");
        return std::nullopt;
    }
    const uint64_t count = uint64_t(*frames) * *channels;
    if (kClipHeaderSize + count * 2 != reader.Size()) {
        Report(ctx.diags, Services::Severity::Error, "prism.clip.size",
               ctx.entry.name + ": declared " + std::to_string(*frames) + " frames does not match the payload");
        return std::nullopt;
    }

    const auto gain = static_cast<float>(ctx.settings.GetDouble(kGainKey).value_or(1.0));
    Domain::Audio audio;
    audio.sampleRate = *rate;
    audio.channels = *channels;
    audio.samples.reserve(static_cast<size_t>(count));
    for (uint64_t i = 0; i < count; ++i) {
        const auto sample = reader.Read<int16_t>();
        if (!sample) {
            Report(ctx.diags, Services::Severity::Error, "prism.clip.truncated", ctx.entry.name + ": samples truncated");
            return std::nullopt;
        }
        audio.samples.push_back(static_cast<float>(*sample) / 32768.0f * gain);
    }
    return audio;
}

std::optional<Domain::Table> DecodeEntries(Modules::DecodeContext& ctx) {
    Vfs::Reader& reader = ctx.reader;
    const auto cols = reader.Read<uint16_t>();
    const auto rows = reader.Read<uint16_t>();
    if (!cols || !rows) {
        Report(ctx.diags, Services::Severity::Error, "prism.entries.truncated",
               ctx.entry.name + ": table header truncated");
        return std::nullopt;
    }
    if (uint64_t(*cols) * *rows * 2 > reader.Remaining()) {
        Report(ctx.diags, Services::Severity::Error, "prism.entries.size",
               ctx.entry.name + ": declared " + std::to_string(*cols) + "x" + std::to_string(*rows) +
                   " cells do not fit the payload");
        return std::nullopt;
    }

    std::vector<std::vector<std::string>> cells;
    for (uint16_t r = 0; r < *rows; ++r) {
        std::vector<std::string>& row = cells.emplace_back();
        for (uint16_t c = 0; c < *cols; ++c) {
            const auto length = reader.Read<uint16_t>();
            std::optional<std::string> cell;
            if (length) cell = reader.ReadString(*length);
            if (!cell) {
                Report(ctx.diags, Services::Severity::Error, "prism.entries.truncated",
                       ctx.entry.name + ": cell " + std::to_string(r) + "," + std::to_string(c) + " truncated");
                return std::nullopt;
            }
            row.push_back(std::move(*cell));
        }
    }

    Domain::Table table;
    if (!cells.empty()) {
        table.columns = std::move(cells.front());
        table.rows.assign(std::make_move_iterator(cells.begin() + 1), std::make_move_iterator(cells.end()));
    }
    return table;
}

std::shared_ptr<const Schema::Schema> StatsSchema() {
    auto schema = std::make_shared<Schema::Schema>("PrismStats");
    schema->Field("imageCount", Schema::FieldType::U32);
    schema->Field("clipCount", Schema::FieldType::U32);
    schema->Field("gainDefault", Schema::FieldType::F32);
    schema->Field("flags", Schema::FieldType::U32);
    return schema;
}

}

Modules::ModuleInfo Module::Info() const {
    return {"prm", "Prism", {"prism"}, {{"Prism files", {"prm"}}}};
}

Modules::ProbeResult Module::Probe(const Modules::ProbeInput& input) const {
    if (StartsWith(input.header, kMagic)) return {95, "PRM1 magic at 0"};
    return {0, "no evidence"};
}

void Module::RegisterTypes(Types::TypeRegistrar& registrar) {
    const auto add = [&](const char* key, const char* label, Domain::MediaKind media, Types::Color color) {
        Types::TypeSpec spec;
        spec.key = key;
        spec.label = label;
        spec.media = media;
        spec.icon = Domain::DefaultIcon(media);
        spec.color = color;
        return registrar.Add(spec);
    };
    m_palette = add("palette", "Palette", Domain::MediaKind::Table, {0.90f, 0.35f, 0.35f, 1.0f});
    m_image = add("image", "Indexed image", Domain::MediaKind::Image, {0.35f, 0.75f, 0.45f, 1.0f});
    m_manifest = add("manifest", "Manifest", Domain::MediaKind::Text, {0.45f, 0.55f, 0.90f, 1.0f});
    m_clip = add("clip", "Clip", Domain::MediaKind::Audio, {0.90f, 0.70f, 0.25f, 1.0f});
    m_entries = add("entries", "Entries", Domain::MediaKind::Table, {0.65f, 0.45f, 0.85f, 1.0f});
    m_stats = add("stats", "Stats", Domain::MediaKind::Raw, {0.30f, 0.80f, 0.85f, 1.0f});
}

void Module::RegisterDecoders(Modules::DecoderRegistry& decoders) {
    decoders.Table(m_palette, DecodePaletteTable);
    decoders.Any(m_palette, std::string(kPaletteCapability), DecodePaletteAny);
    decoders.Image(m_image, DecodeImage);
    decoders.Text(m_manifest, DecodeManifest);
    decoders.Audio(m_clip, DecodeClip);
    decoders.Table(m_entries, DecodeEntries);
    decoders.Schema(m_stats, StatsSchema());
}

void Module::RegisterSettings(Services::SettingsSchema& schema) {
    schema.Bool(std::string(kTransparentIndexZeroKey), false, "Make palette index 0 fully transparent");
    schema.Double(std::string(kGainKey), 1.0, 0.0, 4.0, "Clip gain");
}

Modules::ParseResult Module::ParseContainer(Modules::ContainerContext& ctx) {
    Vfs::Reader& reader = ctx.reader;
    reader.Seek(0);
    const std::optional<std::string> magic = reader.ReadString(kMagic.size());
    if (!magic || *magic != kMagic) {
        Report(ctx.diags, Services::Severity::Error, "prism.header.bad", "missing PRM1 magic");
        return {false};
    }
    const auto paletteCount = reader.Read<uint16_t>();
    auto count = reader.Read<uint32_t>();
    if (!paletteCount || !count) {
        Report(ctx.diags, Services::Severity::Error, "prism.header.truncated", "file too small for the header");
        return {false};
    }

    const uint64_t fileSize = reader.Size();
    const auto maxSections = static_cast<uint32_t>(std::min<uint64_t>(fileSize / kMinSectionHeader, UINT32_MAX));
    if (*count > maxSections) {
        Report(ctx.diags, Services::Severity::Warning, "prism.sections.clamped",
               "section count " + std::to_string(*count) + " exceeds what the file could hold; clamped to " +
                   std::to_string(maxSections));
        *count = maxSections;
    }

    std::vector<SectionHeader> sections;
    sections.reserve(*count);
    for (uint32_t i = 0; i < *count; ++i) {
        const auto kind = reader.Read<uint8_t>();
        const auto nameLen = reader.Read<uint16_t>();
        std::optional<std::string> name;
        if (nameLen) name = reader.ReadString(*nameLen);
        std::optional<uint32_t> offset;
        if (name) offset = reader.Read<uint32_t>();
        std::optional<uint32_t> size;
        if (offset) size = reader.Read<uint32_t>();
        if (!kind || !size) {
            Report(ctx.diags, Services::Severity::Warning, "prism.section.truncated",
                   "section header " + std::to_string(i) + " is truncated");
            break;
        }
        sections.push_back({*kind, std::move(*name), *offset, *size});
    }

    const auto inFile = [&](const SectionHeader& s) { return uint64_t(s.offset) + s.size <= fileSize; };

    std::shared_ptr<Palette> palette;
    for (const SectionHeader& s : sections) {
        if (static_cast<Kind>(s.kind) != Kind::Palette || palette || !inFile(s)) continue;
        if (s.size != uint64_t(*paletteCount) * 4) continue;
        reader.Seek(s.offset);
        std::vector<std::byte> bytes(s.size);
        if (reader.Read(bytes) != bytes.size()) continue;
        palette = std::make_shared<Palette>();
        for (size_t i = 0; i < bytes.size(); i += 4) {
            palette->colors.emplace_back(static_cast<uint8_t>(bytes[i]), static_cast<uint8_t>(bytes[i + 1]),
                                         static_cast<uint8_t>(bytes[i + 2]), static_cast<uint8_t>(bytes[i + 3]));
        }
    }
    ctx.moduleState = palette;

    bool paletteSeen = false;
    ctx.roots.reserve(ctx.roots.size() + sections.size());
    for (const SectionHeader& s : sections) {
        Domain::AssetEntry entry;
        entry.name = s.name;
        entry.source = {0, s.offset, s.size};

        std::string failure;
        std::string code;
        const auto fail = [&](std::string c, std::string why) {
            code = std::move(c);
            failure = std::move(why);
        };

        const auto kind = static_cast<Kind>(s.kind);
        switch (kind) {
            case Kind::Palette:
                entry.type = m_palette;
                entry.kind = Domain::MediaKind::Table;
                if (paletteSeen) fail("prism.palette.duplicate", "a file holds exactly one palette");
                paletteSeen = true;
                break;
            case Kind::Image:
                entry.type = m_image;
                entry.kind = Domain::MediaKind::Image;
                break;
            case Kind::Manifest:
                entry.type = m_manifest;
                entry.kind = Domain::MediaKind::Text;
                break;
            case Kind::Clip:
                entry.type = m_clip;
                entry.kind = Domain::MediaKind::Audio;
                break;
            case Kind::Entries:
                entry.type = m_entries;
                entry.kind = Domain::MediaKind::Table;
                break;
            case Kind::Stats:
                entry.type = m_stats;
                entry.kind = Domain::MediaKind::Raw;
                break;
            default:
                entry.type = m_stats;
                entry.kind = Domain::MediaKind::Raw;
                fail("prism.section.kind", "unknown section kind " + std::to_string(s.kind));
                break;
        }

        if (failure.empty() && !inFile(s)) {
            fail("prism.section.range", "payload out of bounds");
        } else if (failure.empty()) {
            reader.Seek(s.offset);
            if (kind == Kind::Palette && s.size != uint64_t(*paletteCount) * 4) {
                fail("prism.palette.size", "payload is not paletteCount * 4 bytes");
            } else if (kind == Kind::Image) {
                const auto w = reader.Read<uint16_t>();
                const auto h = reader.Read<uint16_t>();
                if (!w || !h || kImageHeaderSize + uint64_t(*w) * *h != s.size) {
                    fail("prism.image.size", "size is not 4 + width * height");
                } else if (!palette) {
                    fail("prism.noPalette", "the file has no palette");
                }
            } else if (kind == Kind::Clip) {
                reader.Seek(s.offset + 4);
                const auto channels = reader.Read<uint16_t>();
                const auto frames = reader.Read<uint32_t>();
                if (!channels || !frames || kClipHeaderSize + uint64_t(*frames) * *channels * 2 != s.size) {
                    fail("prism.clip.size", "size does not match frameCount * channels * 2");
                }
            } else if (kind == Kind::Stats && s.size != kStatsSize) {
                fail("prism.stats.size", "payload is not 16 bytes");
            }
        }

        if (!failure.empty()) {
            entry.flags = Domain::NodeFlags::Failed;
            Report(ctx.diags, Services::Severity::Error, code, entry.name + ": " + failure);
        }
        ctx.roots.push_back(std::move(entry));
    }
    return {true};
}

}
