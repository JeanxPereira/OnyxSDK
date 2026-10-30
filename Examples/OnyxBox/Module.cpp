#include <OnyxBox/Module.h>

#include <OnyxBox/Format.h>

#include <Onyx/Core/Modules/DecoderRegistry.h>
#include <Onyx/Core/Modules/Document.h>
#include <Onyx/Core/Services/SettingsSchema.h>
#include <Onyx/Core/Vfs/IVirtualFileSystem.h>
#include <Onyx/Core/Vfs/MemoryFile.h>
#include <Onyx/Core/Vfs/OsFile.h>
#include <Onyx/Core/Vfs/SliceFile.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>
#include <string>

namespace OnyxBox {

using namespace Onyx;

namespace {

constexpr std::string_view kStrictTocKey = "obx.strictToc";

void Report(Services::DiagSink& diags, Services::Severity severity, std::string code, std::string message) {
    diags.Report(Services::Diag{severity, std::move(code), std::move(message), std::nullopt});
}

std::string Lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool StartsWith(std::span<const std::byte> bytes, std::string_view magic) {
    return bytes.size() >= magic.size() && std::memcmp(bytes.data(), magic.data(), magic.size()) == 0;
}

struct PakEntry {
    std::string name;
    uint64_t offset = 0;
    uint64_t size = 0;
};

class PakFileSystem final : public Vfs::IVirtualFileSystem {
public:
    PakFileSystem(std::shared_ptr<const Vfs::IFile> pak, std::vector<PakEntry> entries)
        : m_pak(std::move(pak)), m_entries(std::move(entries)) {}

    std::vector<Vfs::DirEntry> List(std::string_view) const override {
        std::vector<Vfs::DirEntry> out;
        out.reserve(m_entries.size());
        for (const PakEntry& e : m_entries) out.push_back({e.name, false, e.size});
        return out;
    }

    std::shared_ptr<Vfs::IFile> Open(std::string_view path) const override {
        const PakEntry* e = Find(path);
        if (!e) return nullptr;
        return std::make_shared<Vfs::SliceFile>(m_pak, e->offset, e->size);
    }

    bool Exists(std::string_view path) const override { return Find(path) != nullptr; }

private:
    const PakEntry* Find(std::string_view path) const {
        const auto it =
            std::find_if(m_entries.begin(), m_entries.end(), [&](const PakEntry& e) { return e.name == path; });
        return it == m_entries.end() ? nullptr : &*it;
    }

    std::shared_ptr<const Vfs::IFile> m_pak;
    std::vector<PakEntry> m_entries;
};

// Refuses the mount on a bad header. Once the header is good, a record that cannot be read or
// whose size exceeds the remaining bytes ends the walk and keeps the records before it.
std::shared_ptr<Vfs::IVirtualFileSystem> MountPak(const std::filesystem::path& path) {
    auto os = Vfs::OsFile::Open(path);
    if (!os.ok()) return nullptr;

    Vfs::Reader whole(os.value());
    auto pak = std::make_shared<Vfs::MemoryFile>(whole.ReadAll());

    Vfs::Reader reader(pak);
    const std::optional<std::string> magic = reader.ReadString(kPakMagic.size());
    if (!magic || *magic != kPakMagic) return nullptr;
    const std::optional<uint32_t> count = reader.Read<uint32_t>();
    if (!count) return nullptr;

    const uint64_t maxRecords = reader.Remaining() / kMinPakRecord;
    const uint64_t records = std::min<uint64_t>(*count, maxRecords);

    std::vector<PakEntry> entries;
    entries.reserve(static_cast<size_t>(records));
    for (uint64_t i = 0; i < records; ++i) {
        const auto nameLen = reader.Read<uint32_t>();
        if (!nameLen || *nameLen < 1 || *nameLen > kMaxPakNameLength) break;
        auto name = reader.ReadString(*nameLen);
        if (!name) break;
        const auto size = reader.Read<uint32_t>();
        if (!size || *size > reader.Remaining()) break;

        entries.push_back({std::move(*name), reader.Tell(), *size});
        reader.Seek(reader.Tell() + *size);
    }
    return std::make_shared<PakFileSystem>(std::move(pak), std::move(entries));
}

// Each face has its own four vertices so the flat normals stay exact.
Domain::MeshPart BuildBox(const glm::vec3& center, const glm::vec3& halfExtents) {
    struct Face {
        glm::vec3 normal, uAxis, vAxis;
    };
    static const std::array<Face, 6> kFaces = {{
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    }};

    Domain::MeshPart part;
    part.name = "box";
    for (const Face& face : kFaces) {
        const glm::vec3 faceCenter = center + face.normal * halfExtents;
        const glm::vec3 u = face.uAxis * halfExtents;
        const glm::vec3 v = face.vAxis * halfExtents;
        const auto base = static_cast<uint32_t>(part.positions.size());
        part.positions.push_back(faceCenter - u - v);
        part.positions.push_back(faceCenter + u - v);
        part.positions.push_back(faceCenter + u + v);
        part.positions.push_back(faceCenter - u + v);
        part.normals.insert(part.normals.end(), 4, face.normal);
        part.indices.insert(part.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    part.bounds = Domain::BoundingBox::Of(part.positions);
    return part;
}

std::optional<Domain::Texture> DecodeImage(Modules::DecodeContext& ctx) {
    Vfs::Reader& reader = ctx.reader;
    if (reader.Size() < 4) {
        Report(ctx.diags, Services::Severity::Error, "obx.image.range", "image payload out of bounds");
        return std::nullopt;
    }

    const auto width = reader.Read<uint16_t>();
    const auto height = reader.Read<uint16_t>();
    if (!width || !height) {
        Report(ctx.diags, Services::Severity::Error, "obx.image.truncated", "image header truncated");
        return std::nullopt;
    }

    const uint64_t pixelBytes = uint64_t(*width) * uint64_t(*height) * 4;
    if (4 + pixelBytes != reader.Size()) {
        Report(ctx.diags, Services::Severity::Error, "obx.image.size-mismatch",
               ctx.entry.name + ": declared " + std::to_string(*width) + "x" + std::to_string(*height) +
                   " does not match the payload");
        return std::nullopt;
    }

    Domain::Texture texture;
    texture.name = ctx.entry.name;
    texture.width = *width;
    texture.height = *height;
    texture.format = Domain::PixelFormat::Rgba8;
    texture.pixels.resize(static_cast<size_t>(pixelBytes));
    if (reader.Read(texture.pixels) != pixelBytes) {
        Report(ctx.diags, Services::Severity::Error, "obx.image.truncated", "pixel data truncated");
        return std::nullopt;
    }
    return texture;
}

std::optional<Domain::Text> DecodeText(Modules::DecodeContext& ctx) {
    const std::vector<std::byte> bytes = ctx.reader.ReadAll();
    Domain::Text text;
    text.text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return text;
}

std::unique_ptr<Domain::Scene> DecodeMesh(Modules::DecodeContext& ctx) {
    Vfs::Reader& reader = ctx.reader;
    if (reader.Size() != kMeshPayloadSize) {
        Report(ctx.diags, Services::Severity::Error, "obx.mesh.size",
               ctx.entry.name + ": mesh payload must be exactly " + std::to_string(kMeshPayloadSize) +
                   " bytes (12 floats), got " + std::to_string(reader.Size()));
        return nullptr;
    }

    std::array<float, 12> values{};
    for (float& v : values) {
        const auto read = reader.Read<float>();
        if (!read) {
            Report(ctx.diags, Services::Severity::Error, "obx.mesh.truncated", "mesh payload truncated");
            return nullptr;
        }
        v = *read;
    }

    auto scene = std::make_unique<Domain::Scene>();
    const glm::vec3 center(values[4], values[5], values[6]);
    const glm::vec3 halfExtents(std::fabs(values[7]), std::fabs(values[8]), std::fabs(values[9]));
    scene->parts.push_back(BuildBox(center, halfExtents));

    Domain::Material material;
    material.name = "box";
    material.baseColor = glm::vec4(values[0], values[1], values[2], values[3]);
    scene->materials.push_back(std::move(material));
    scene->bounds = scene->ComputeBounds();
    return scene;
}

std::optional<Domain::Table> DecodeTable(Modules::DecodeContext& ctx) {
    Vfs::Reader& reader = ctx.reader;
    const auto cols = reader.Read<uint16_t>();
    const auto rows = reader.Read<uint16_t>();
    if (!cols || !rows) {
        Report(ctx.diags, Services::Severity::Error, "obx.table.truncated",
               ctx.entry.name + ": table header truncated");
        return std::nullopt;
    }

    const uint64_t cells = uint64_t(*cols) * uint64_t(*rows);
    if (cells * 2 > reader.Remaining()) {
        Report(ctx.diags, Services::Severity::Error, "obx.table.size",
               ctx.entry.name + ": declared " + std::to_string(*cols) + "x" + std::to_string(*rows) +
                   " cells do not fit the payload");
        return std::nullopt;
    }

    Domain::Table table;
    for (uint16_t c = 0; c < *cols; ++c) table.columns.push_back("col" + std::to_string(c));
    table.rows.reserve(*rows);
    for (uint16_t r = 0; r < *rows; ++r) {
        std::vector<std::string>& row = table.rows.emplace_back();
        row.reserve(*cols);
        for (uint16_t c = 0; c < *cols; ++c) {
            const auto length = reader.Read<uint16_t>();
            std::optional<std::string> cell;
            if (length) cell = reader.ReadString(*length);
            if (!cell) {
                Report(ctx.diags, Services::Severity::Error, "obx.table.truncated",
                       ctx.entry.name + ": cell " + std::to_string(r) + "," + std::to_string(c) + " truncated");
                return std::nullopt;
            }
            row.push_back(std::move(*cell));
        }
    }
    if (reader.Remaining() != 0) {
        Report(ctx.diags, Services::Severity::Error, "obx.table.size",
               ctx.entry.name + ": " + std::to_string(reader.Remaining()) + " bytes follow the last cell");
        return std::nullopt;
    }
    return table;
}

}

Modules::ModuleInfo Module::Info() const {
    return {"obx", "OnyxBox (example)", {"obx"}, {{"OnyxBox", {"obx"}}, {"OnyxBox pak", {"obxpak"}}}};
}

Modules::ProbeResult Module::Probe(const Modules::ProbeInput& input) const {
    if (StartsWith(input.header, kBoxMagic)) return {95, "OBX1 magic at 0"};
    if (StartsWith(input.header, kPakMagic)) return {95, "OBP1 magic at 0"};

    const std::string ext = Lowercase(input.path.extension().string());
    if (ext == ".obx" || ext == ".obxpak") return {20, "extension only"};
    return {0, "no evidence"};
}

void Module::RegisterTypes(Types::TypeRegistrar& registrar) {
    const auto add = [&](const char* key, const char* label, Domain::MediaKind media, Types::IconId icon) {
        Types::TypeSpec spec;
        spec.key = key;
        spec.label = label;
        spec.media = media;
        spec.icon = icon;
        return registrar.Add(spec);
    };
    m_blob = add("blob", "Blob", Domain::MediaKind::Raw, Types::IconId::Raw);
    m_image = add("image", "Image", Domain::MediaKind::Image, Types::IconId::Image);
    m_text = add("text", "Text", Domain::MediaKind::Text, Types::IconId::Text);
    m_mesh = add("mesh", "Mesh", Domain::MediaKind::Mesh, Types::IconId::Mesh);
    m_table = add("table", "Table", Domain::MediaKind::Table, Types::IconId::Table);
    m_folder = registrar.Find("onyx.folder");
}

void Module::RegisterDecoders(Modules::DecoderRegistry& decoders) {
    decoders.Image(m_image, DecodeImage);
    decoders.Text(m_text, DecodeText);
    decoders.Scene(m_mesh, DecodeMesh);
    decoders.Table(m_table, DecodeTable);
}

void Module::RegisterSettings(Services::SettingsSchema& schema) {
    schema.Bool(std::string(kStrictTocKey), false, "Fail the parse when the table of contents needed salvage");
}

std::vector<Modules::MountSpec> Module::Mounts() const {
    return {Modules::MountSpec{"OnyxBox pak", {"obxpak"}, MountPak}};
}

void Module::Classify(uint8_t kind, Domain::AssetEntry& entry) const {
    switch (static_cast<Kind>(kind)) {
        case Kind::Image:
            entry.type = m_image;
            entry.kind = Domain::MediaKind::Image;
            break;
        case Kind::Text:
            entry.type = m_text;
            entry.kind = Domain::MediaKind::Text;
            break;
        case Kind::Mesh:
            entry.type = m_mesh;
            entry.kind = Domain::MediaKind::Mesh;
            break;
        case Kind::Table:
            entry.type = m_table;
            entry.kind = Domain::MediaKind::Table;
            break;
        default:
            entry.type = m_blob;
            entry.kind = Domain::MediaKind::Raw;
            break;
    }
}

Module::Walk Module::WalkToc(Vfs::Reader& reader, uint32_t fileIndex, Services::DiagSink& diags,
                             std::vector<Domain::AssetEntry>& out) const {
    reader.Seek(0);
    const std::optional<std::string> magic = reader.ReadString(kBoxMagic.size());
    if (!magic || *magic != kBoxMagic) {
        Report(diags, Services::Severity::Error, "obx.header.bad", "missing OBX1 magic");
        return Walk::Bad;
    }
    std::optional<uint32_t> count = reader.Read<uint32_t>();
    if (!count) {
        Report(diags, Services::Severity::Error, "obx.header.truncated", "file too small for the TOC count");
        return Walk::Bad;
    }

    Walk walk = Walk::Clean;
    const uint64_t fileSize = reader.Size();
    const auto maxEntries = static_cast<uint32_t>(std::min<uint64_t>(fileSize / kMinTocRecord, UINT32_MAX));
    if (*count > maxEntries) {
        Report(diags, Services::Severity::Warning, "obx.toc.count-clamped",
               "TOC count " + std::to_string(*count) + " exceeds what the file could hold; clamped to " +
                   std::to_string(maxEntries));
        *count = maxEntries;
        walk = Walk::Salvaged;
    }
    out.reserve(out.size() + *count);

    for (uint32_t i = 0; i < *count; ++i) {
        const auto nameLen = reader.Read<uint16_t>();
        std::optional<std::string> name;
        if (nameLen) name = reader.ReadString(*nameLen);
        std::optional<uint8_t> kind;
        if (name) kind = reader.Read<uint8_t>();
        std::optional<uint32_t> offset;
        if (kind) offset = reader.Read<uint32_t>();
        std::optional<uint32_t> size;
        if (offset) size = reader.Read<uint32_t>();
        if (!size) {
            Report(diags, Services::Severity::Warning, "obx.toc.truncated",
                   "TOC entry " + std::to_string(i) + " is truncated");
            walk = Walk::Salvaged;
            break;
        }

        Domain::AssetEntry entry;
        entry.name = std::move(*name);
        entry.source = {fileIndex, *offset, *size};
        Classify(*kind, entry);

        if (uint64_t(*offset) > fileSize || uint64_t(*offset) + *size > fileSize) {
            entry.flags = Domain::NodeFlags::Failed;
            Report(diags, Services::Severity::Error, "obx.entry.range", entry.name + ": payload out of bounds");
        } else if (static_cast<Kind>(*kind) == Kind::Mesh && *size != kMeshPayloadSize) {
            entry.flags = Domain::NodeFlags::Failed;
            Report(diags, Services::Severity::Error, "obx.mesh.size",
                   entry.name + ": mesh payload must be exactly " + std::to_string(kMeshPayloadSize) +
                       " bytes (12 floats), got " + std::to_string(*size));
        }
        out.push_back(std::move(entry));
    }
    return walk;
}

Modules::ParseResult Module::ParseContainer(Modules::ContainerContext& ctx) {
    const bool strict = ctx.settings.GetBool(kStrictTocKey).value_or(false);

    if (ctx.mounted) {
        const std::vector<Vfs::DirEntry> inner = ctx.mounted->List("");
        if (inner.empty()) {
            Report(ctx.diags, Services::Severity::Warning, "obx.pak.empty",
                   "mounted archive lists no inner containers");
        }

        bool salvaged = false;
        for (const Vfs::DirEntry& item : inner) {
            std::shared_ptr<Vfs::IFile> file = ctx.mounted->Open(item.name);
            if (!file) {
                Report(ctx.diags, Services::Severity::Warning, "obx.pak.openFailed",
                       "could not open inner container: " + item.name);
                continue;
            }

            const auto fileIndex = static_cast<uint32_t>(ctx.files.size());
            ctx.files.push_back(file);

            Domain::AssetEntry subtree;
            subtree.name = item.name;
            subtree.type = m_folder;
            subtree.kind = Domain::MediaKind::Container;
            Vfs::Reader reader(file);
            const Walk walk = WalkToc(reader, fileIndex, ctx.diags, subtree.children);
            if (walk == Walk::Bad) subtree.flags = Domain::NodeFlags::Failed;
            salvaged = salvaged || walk == Walk::Salvaged;
            ctx.roots.push_back(std::move(subtree));
        }
        return {!(strict && salvaged)};
    }

    const Walk walk = WalkToc(ctx.reader, 0, ctx.diags, ctx.roots);
    if (walk == Walk::Bad) return {false};
    return {!(strict && walk == Walk::Salvaged)};
}

}
