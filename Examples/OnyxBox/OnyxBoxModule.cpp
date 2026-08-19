#include "OnyxBoxModule.h"

#include <Onyx/Modules/Workspace.h>
#include <Onyx/Vfs/IFile.h>
#include <Onyx/Vfs/IVirtualFileSystem.h>
#include <Onyx/Vfs/OsFile.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace OnyxBox {

using Onyx::Modules::ContainerContext;
using Onyx::Modules::DecodeContext;
using Onyx::Modules::DecoderRegistry;
using Onyx::Modules::Document;
using Onyx::Modules::ModuleInfo;
using Onyx::Modules::OpenFilter;
using Onyx::Modules::ParseResult;
using Onyx::Modules::ProbeInput;
using Onyx::Modules::ProbeResult;
using Onyx::Modules::TextOut;
using Onyx::Services::Diag;
using Onyx::Services::Severity;

namespace {

// ── Little-endian primitive readers ────────────────────────────────────────
// Never trust the count returned by Read(): a short read means the file
// ended before the field did, and the caller must stop cleanly rather than
// use a partially-filled value.

bool ReadU16LE(Onyx::Vfs::IFile& f, uint16_t& out) {
    uint8_t b[2];
    if (f.Read(b, sizeof(b)) != sizeof(b)) return false;
    out = uint16_t(b[0]) | (uint16_t(b[1]) << 8);
    return true;
}

bool ReadU32LE(Onyx::Vfs::IFile& f, uint32_t& out) {
    uint8_t b[4];
    if (f.Read(b, sizeof(b)) != sizeof(b)) return false;
    out = uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
    return true;
}

// Mesh payload floats (kind=3) are stored the same little-endian way as
// every other multi-byte field in this format -- read as raw bits, then
// reinterpreted, rather than a direct `file.Read(&f, 4)` that would trust
// the host's own float byte order (harmless on every platform this repo
// targets today, but ReadU16LE/ReadU32LE above already made that
// endian-explicit choice for integers, so this mirrors it for floats too).
bool ReadF32LE(Onyx::Vfs::IFile& f, float& out) {
    uint32_t bits = 0;
    if (!ReadU32LE(f, bits)) return false;
    std::memcpy(&out, &bits, sizeof(out));
    return true;
}

// kind=3 (mesh) payload size: baseColor RGBA (4) + position xyz (3) +
// half-extents xyz (3) + 2 padding floats = 12 f32 = 48 bytes, exactly.
// Shared between the ParseObxToc hardening check and DecodeMesh's own
// (redundant but cheap) re-check against the live TocEntry.
constexpr uint32_t kMeshPayloadSize = 12 * 4;

bool ReadU32LEFromBuffer(const std::vector<uint8_t>& buf, size_t pos, uint32_t& out) {
    if (pos + 4 > buf.size()) return false;
    out = uint32_t(buf[pos]) | (uint32_t(buf[pos + 1]) << 8) |
          (uint32_t(buf[pos + 2]) << 16) | (uint32_t(buf[pos + 3]) << 24);
    return true;
}

// Looks up the parsed TOC entry matching `name` in file-table slot
// `fileIndex`, stashed as ModuleState by ParseContainer -- the decoder
// never re-walks the file to find it. Filtering by fileIndex (not just
// name) matters once an obxpak mount is in play: two inner OBX containers
// are free to reuse the same entry name with different bytes, and only
// the (fileIndex, name) pair identifies one unambiguously.
const TocEntry* FindTocEntry(Document& doc, const std::string& name, uint32_t fileIndex) {
    if (!doc.state) return nullptr;
    auto* toc = static_cast<std::vector<TocEntry>*>(doc.state.get());
    for (const auto& e : *toc) {
        if (e.fileIndex == fileIndex && e.name == name) return &e;
    }
    return nullptr;
}

// ── OBXPAK mount machinery (Task 8) ─────────────────────────────────────────
// One inner file's location within the pak's byte buffer.
struct ObxPakEntry {
    std::string name;
    size_t      offset = 0;   // start of its raw OBX1 bytes within the pak
    size_t      size = 0;
};

// In-memory IFile serving a fixed slice [offset, offset+size) of a shared
// pak byte buffer -- backs each inner OBX container an obxpak mount hands
// out through ObxPakVfs::OpenFile.
class ObxPakFile : public Onyx::Vfs::IFile {
public:
    ObxPakFile(std::shared_ptr<const std::vector<uint8_t>> bytes, size_t offset, size_t size)
        : m_bytes(std::move(bytes)), m_offset(offset), m_size(size) {}

    size_t Read(void* dest, size_t bytes) override {
        size_t avail = m_pos < int64_t(m_size) ? m_size - size_t(m_pos) : 0;
        size_t n = bytes < avail ? bytes : avail;
        if (n > 0) std::memcpy(dest, m_bytes->data() + m_offset + size_t(m_pos), n);
        m_pos += int64_t(n);
        return n;
    }
    bool Seek(int64_t offset, int origin) override {
        int64_t base = 0;
        if (origin == SEEK_CUR) base = m_pos;
        else if (origin == SEEK_END) base = int64_t(m_size);
        int64_t next = base + offset;
        if (next < 0 || next > int64_t(m_size)) return false;
        m_pos = next;
        return true;
    }
    int64_t Tell() override { return m_pos; }
    size_t Size() const override { return m_size; }
    bool IsEOF() const override { return m_pos >= int64_t(m_size); }
    bool IsValid() const override { return true; }

private:
    std::shared_ptr<const std::vector<uint8_t>> m_bytes;
    size_t m_offset;
    size_t m_size;
    int64_t m_pos = 0;
};

// In-memory VFS over a parsed obxpak's directory: ListDirectory returns the
// inner OBX1 container names in TOC order, OpenFile hands back an
// ObxPakFile view over each one's byte range within the pak.
class ObxPakVfs : public Onyx::Vfs::IVirtualFileSystem {
public:
    ObxPakVfs(std::shared_ptr<const std::vector<uint8_t>> bytes, std::vector<ObxPakEntry> entries)
        : m_bytes(std::move(bytes)), m_entries(std::move(entries)) {}

    bool IsValid() const override { return true; }

    std::vector<std::string> ListDirectory(const std::string&) override {
        std::vector<std::string> names;
        names.reserve(m_entries.size());
        for (const auto& e : m_entries) names.push_back(e.name);
        return names;
    }

    std::unique_ptr<Onyx::Vfs::IFile> OpenFile(const std::string& path) override {
        for (const auto& e : m_entries) {
            if (e.name == path) return std::make_unique<ObxPakFile>(m_bytes, e.offset, e.size);
        }
        return nullptr;
    }

    bool Exists(const std::string& path) override {
        for (const auto& e : m_entries) {
            if (e.name == path) return true;
        }
        return false;
    }

private:
    std::shared_ptr<const std::vector<uint8_t>> m_bytes;
    std::vector<ObxPakEntry> m_entries;
};

// Mount factory for MountSpec{"obxpak"}: parses the OBXPAK header (magic
// "OBP1", u32 count, then per file u32 nameLen | name | u32 size | raw OBX1
// bytes) and returns a VFS listing every inner container. MountSpec::mount
// takes only a path -- no diag channel reaches this far -- so hardening
// here is silent and conservative: any header-level failure (bad magic, a
// truncated count) refuses the whole mount by returning nullptr, which
// Workspace treats as a refused mount and falls through to a flat-file
// parse with its own Warning diag (never an abort). Once the header is
// good, a per-file TOC record whose fields can't even be read, or whose
// declared size doesn't fit the remaining bytes, stops the walk right
// there (nothing past an unparseable boundary can be trusted) but keeps
// every entry already parsed -- mirroring the OBX1 TOC walker's own
// truncation handling below.
std::shared_ptr<Onyx::Vfs::IVirtualFileSystem> MountObxPak(const std::filesystem::path& path) {
    Onyx::Vfs::OsFile file(path.string());
    if (!file.IsValid()) return nullptr;

    auto bytes = std::make_shared<std::vector<uint8_t>>(file.ReadAll());
    const std::vector<uint8_t>& buf = *bytes;

    if (buf.size() < 8 || std::memcmp(buf.data(), "OBP1", 4) != 0) return nullptr;

    uint32_t count = 0;
    if (!ReadU32LEFromBuffer(buf, 4, count)) return nullptr;

    // Smallest possible packed file record: nameLen(4) + name(1) + size(4)
    // = 9 bytes, with an empty payload -- clamp the claimed count to what
    // the remaining bytes could physically hold before trusting it.
    constexpr uint32_t kMinEntrySize = 4 + 1 + 4;
    const uint64_t remaining = buf.size() - 8;
    const uint32_t maxPossible =
        uint32_t(std::min<uint64_t>(remaining / kMinEntrySize, UINT32_MAX));
    if (count > maxPossible) count = maxPossible;

    std::vector<ObxPakEntry> entries;
    entries.reserve(count);
    size_t pos = 8;

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t nameLen = 0;
        if (!ReadU32LEFromBuffer(buf, pos, nameLen)) break;
        pos += 4;

        // nameLen clamp: 1..255, mirroring the OBX1 TOC's own hardening.
        if (nameLen < 1 || nameLen > 255) break;
        if (pos + nameLen > buf.size()) break;
        std::string name(reinterpret_cast<const char*>(buf.data() + pos), nameLen);
        pos += nameLen;

        uint32_t size = 0;
        if (!ReadU32LEFromBuffer(buf, pos, size)) break;
        pos += 4;

        if (uint64_t(pos) + uint64_t(size) > buf.size()) break;   // declared size vs remaining bytes

        entries.push_back(ObxPakEntry{std::move(name), pos, size});
        pos += size;
    }

    return std::make_shared<ObxPakVfs>(std::move(bytes), std::move(entries));
}

// Builds a single-part axis-aligned box centered at `center` with the given
// per-axis `halfExtents`: 6 faces * 4 vertices (24 total, not the 8-vertex
// shared-corner shortcut) so every face gets a correct outward flat normal
// and its own 0..1 UV square, matching how a real "cube" primitive is
// conventionally authored (shared vertices would average adjacent faces'
// normals into a smoothed, visually wrong result for a box). Object-space
// (no instanceTransform applied -- SceneData::instanceTransform stays
// identity for this decoder).
Onyx::Parsers::MeshPart BuildCubePart(const glm::vec3& center, const glm::vec3& halfExtents) {
    using Onyx::Domain::GpuVertex;

    Onyx::Parsers::MeshPart part;
    part.name = "cube";
    part.materialId = 0;
    part.useBindToJoint = false; // static geometry, no skeleton

    struct Face { glm::vec3 normal, uAxis, vAxis; };
    // uAxis/vAxis are unit basis directions (scaled by halfExtents below,
    // per-face) chosen so every face's 4 corners wind counter-clockwise
    // when viewed from outside along -normal (consistent front-face
    // winding, even though the scene pipelines currently render with
    // VK_CULL_MODE_NONE -- Source/RenderVk/Pipelines.cpp -- so this does
    // not yet affect visibility, only correctness-by-convention).
    const std::array<Face, 6> kFaces = {{
        {{ 1, 0, 0}, {0, 0,-1}, {0, 1, 0}},  // +X
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},  // -X
        {{ 0, 1, 0}, {1, 0, 0}, {0, 0,-1}},  // +Y
        {{ 0,-1, 0}, {1, 0, 0}, {0, 0, 1}},  // -Y
        {{ 0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  // +Z
        {{ 0, 0,-1}, {-1, 0, 0}, {0, 1, 0}}, // -Z
    }};
    const glm::vec2 kFaceUv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

    for (const Face& face : kFaces) {
        const glm::vec3 faceCenter = center + face.normal * halfExtents;
        const glm::vec3 uAxis = face.uAxis * halfExtents;
        const glm::vec3 vAxis = face.vAxis * halfExtents;
        const glm::vec3 corners[4] = {
            faceCenter - uAxis - vAxis,
            faceCenter + uAxis - vAxis,
            faceCenter + uAxis + vAxis,
            faceCenter - uAxis + vAxis,
        };

        const uint32_t base = uint32_t(part.vertices.size());
        for (int i = 0; i < 4; ++i) {
            GpuVertex vert{};
            vert.position = corners[i];
            vert.normal = face.normal;
            vert.uv = kFaceUv[i];
            part.vertices.push_back(vert);
        }
        part.indices.insert(part.indices.end(), {base + 0, base + 1, base + 2,
                                                   base + 0, base + 2, base + 3});
    }

    return part;
}

} // namespace

ModuleInfo OnyxBoxModule::Info() const {
    return ModuleInfo{
        "obx",
        "OnyxBox (example)",
        {"obx"},
        {OpenFilter{"OnyxBox", {"obx"}}, OpenFilter{"OnyxBox pak", {"obxpak"}}},
    };
}

ProbeResult OnyxBoxModule::Probe(const ProbeInput& in) const {
    if (in.header.size() >= 4) {
        if (std::memcmp(in.header.data(), "OBX1", 4) == 0) {
            return ProbeResult{95, "OBX1 magic at 0"};
        }
        if (std::memcmp(in.header.data(), "OBP1", 4) == 0) {
            return ProbeResult{95, "OBP1 magic at 0"};
        }
    }

    std::string ext = in.path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](unsigned char c) { return char(std::tolower(c)); });
    if (ext == ".obx" || ext == ".obxpak") {
        return ProbeResult{20, "extension only"};
    }

    return ProbeResult{0, "no evidence"};
}

void OnyxBoxModule::RegisterTypes(Onyx::Types::TypeRegistrar& r) {
    Onyx::Types::TypeInfo image;
    image.key = "image";
    image.label = "Image";
    image.media = Onyx::Domain::MediaKind::Image;
    m_imageType = r.Add(image);

    // MediaKind has no dedicated "Text" member (see Include/Onyx/Domain/
    // MediaKind.h); Unknown is the closest fit for a raw-UTF8 payload kind.
    Onyx::Types::TypeInfo text;
    text.key = "text";
    text.label = "Text";
    text.media = Onyx::Domain::MediaKind::Unknown;
    m_textType = r.Add(text);

    Onyx::Types::TypeInfo blob;
    blob.key = "blob";
    blob.label = "Blob";
    blob.media = Onyx::Domain::MediaKind::Raw;
    m_blobType = r.Add(blob);

    Onyx::Types::TypeInfo mesh;
    mesh.key = "mesh";
    mesh.label = "Mesh";
    mesh.media = Onyx::Domain::MediaKind::Mesh;
    m_meshType = r.Add(mesh);
}

void OnyxBoxModule::RegisterDecoders(DecoderRegistry& reg) {
    reg.Image(m_imageType, &OnyxBoxModule::DecodeImage);
    reg.Text(m_textType, &OnyxBoxModule::DecodeText);
    reg.Scene(m_meshType, &OnyxBoxModule::DecodeMesh);
}

std::vector<Onyx::Modules::MountSpec> OnyxBoxModule::Mounts() const {
    Onyx::Modules::MountSpec spec;
    spec.label = "OnyxBox pak";
    spec.extensions = {"obxpak"};
    spec.mount = [](const std::filesystem::path& path) {
        return MountObxPak(path);
    };
    return {spec};
}

bool OnyxBoxModule::ParseObxToc(Onyx::Vfs::IFile& file, uint32_t fileIndex,
                                 Onyx::Services::DiagSink& diags,
                                 std::vector<Onyx::Domain::AssetEntry>& outEntries,
                                 std::vector<TocEntry>& outToc) {
    file.Seek(0, SEEK_SET);
    char magic[4] = {};
    if (file.Read(magic, sizeof(magic)) != sizeof(magic) ||
        std::memcmp(magic, "OBX1", 4) != 0) {
        diags.Report(Diag{Severity::Error, "obx.header.bad",
                           "missing OBX1 magic", std::nullopt});
        return false;
    }

    uint32_t count = 0;
    if (!ReadU32LE(file, count)) {
        diags.Report(Diag{Severity::Error, "obx.header.truncated",
                           "file too small for TOC count", std::nullopt});
        return false;
    }

    const uint64_t fileSize = file.Size();

    // Never trust the raw count for allocation: a corrupt/hostile header can
    // claim billions of entries. Clamp to what the file could physically
    // hold (the smallest possible packed entry is nameLen(2) + kind(1) +
    // payloadOffset(4) + payloadSize(4) = 11 bytes, with an empty name)
    // before reserving.
    constexpr uint32_t kMinEntrySize = 2 + 1 + 4 + 4;
    const uint32_t maxPossibleEntries =
        uint32_t(std::min<uint64_t>(fileSize / kMinEntrySize, UINT32_MAX));
    if (count > maxPossibleEntries) {
        diags.Report(Diag{Severity::Warning, "obx.toc.count-clamped",
                           "TOC count " + std::to_string(count) +
                               " exceeds what the file could hold; clamped to " +
                               std::to_string(maxPossibleEntries),
                           std::nullopt});
        count = maxPossibleEntries;
    }

    outEntries.reserve(outEntries.size() + count);
    outToc.reserve(outToc.size() + count);

    for (uint32_t i = 0; i < count; ++i) {
        uint16_t nameLen = 0;
        if (!ReadU16LE(file, nameLen)) {
            diags.Report(Diag{Severity::Warning, "obx.toc.truncated",
                               "TOC entry " + std::to_string(i) +
                                   " truncated reading nameLen",
                               std::nullopt});
            break;
        }

        std::string name(nameLen, '\0');
        if (nameLen > 0 && file.Read(name.data(), nameLen) != nameLen) {
            diags.Report(Diag{Severity::Warning, "obx.toc.truncated",
                               "TOC entry " + std::to_string(i) +
                                   " truncated reading name",
                               std::nullopt});
            break;
        }

        uint8_t kind = 0;
        if (file.Read(&kind, 1) != 1) {
            diags.Report(Diag{Severity::Warning, "obx.toc.truncated",
                               "TOC entry " + std::to_string(i) +
                                   " truncated reading kind",
                               std::nullopt});
            break;
        }

        uint32_t payloadOffset = 0, payloadSize = 0;
        if (!ReadU32LE(file, payloadOffset) || !ReadU32LE(file, payloadSize)) {
            diags.Report(Diag{Severity::Warning, "obx.toc.truncated",
                               "TOC entry " + std::to_string(i) +
                                   " truncated reading payload range",
                               std::nullopt});
            break;
        }

        outToc.push_back(TocEntry{name, kind, payloadOffset, payloadSize, fileIndex});

        Onyx::Domain::AssetEntry entry;
        entry.name = name;
        entry.source.fileIndex = fileIndex;
        entry.source.offset = payloadOffset;
        entry.source.size = payloadSize;

        switch (kind) {
            case 1:
                entry.typeId = m_imageType;
                entry.kind = Onyx::Domain::MediaKind::Image;
                break;
            case 2:
                entry.typeId = m_textType;
                entry.kind = Onyx::Domain::MediaKind::Unknown;
                break;
            case 3:
                entry.typeId = m_meshType;
                entry.kind = Onyx::Domain::MediaKind::Mesh;
                break;
            default:
                entry.typeId = m_blobType;
                entry.kind = Onyx::Domain::MediaKind::Raw;
                break;
        }

        const bool outOfBounds =
            uint64_t(payloadOffset) > fileSize ||
            uint64_t(payloadOffset) + uint64_t(payloadSize) > fileSize;

        if (outOfBounds) {
            entry.flags = Onyx::Domain::NodeFlags::Failed;
            diags.Report(Diag{Severity::Error, "obx.entry.range",
                               "payload out of bounds", std::nullopt});
        } else if (kind == 3 && payloadSize != kMeshPayloadSize) {
            // Task 14 hardening: a mesh entry's payload must be exactly 12
            // little-endian floats (48 bytes -- see this file's top
            // comment for the layout). Caught here, at TOC-walk time, not
            // inside DecodeMesh: DecodeContext::entry is a const reference
            // into this same tree node, so only parse time can ever mark
            // it Failed (mirrors the outOfBounds branch above). The walk
            // never aborts -- this entry is flagged and kept, the next TOC
            // record still parses normally (salvage, spec sec7.1).
            entry.flags = Onyx::Domain::NodeFlags::Failed;
            diags.Report(Diag{Severity::Error, "obx.mesh.size",
                               "mesh payload must be exactly " +
                                   std::to_string(kMeshPayloadSize) + " bytes (12 floats), got " +
                                   std::to_string(payloadSize),
                               std::nullopt});
        }

        outEntries.push_back(std::move(entry));
    }

    return true;
}

ParseResult OnyxBoxModule::ParseContainer(ContainerContext& ctx) {
    auto toc = std::make_shared<std::vector<TocEntry>>();

    if (ctx.mountedVfs && ctx.fileTable) {
        // OBXPAK mount (Task 8): the opened file IS the pak; every inner
        // OBX1 container it names gets opened through the mount, pushed
        // into the Document's file table, and parsed with the same TOC
        // walker as a flat .obx -- into its own child subtree named after
        // the inner file, stamped with that file's table slot. An inner
        // file that fails to even open (the mount's own defensive walk
        // already dropped anything unparseable from its directory) is
        // skipped with a diag; one whose OBX1 header itself is bad still
        // gets a subtree, flagged Failed, rather than aborting the pak.
        const std::vector<std::string> innerNames = ctx.mountedVfs->ListDirectory(std::string());
        if (innerNames.empty()) {
            // A mount that succeeded but named nothing (an empty OBP1 header,
            // or one whose first record was truncated) is worth flagging --
            // otherwise this looks silently identical to an intentionally
            // empty pak. Still not a reason to fail the open: no roots is a
            // valid (if useless) result.
            ctx.diags.Report(Diag{Severity::Warning, "onyxbox.pak-empty",
                                   "mounted archive lists no inner containers",
                                   std::nullopt});
        }
        for (const std::string& name : innerNames) {
            std::unique_ptr<Onyx::Vfs::IFile> inner = ctx.mountedVfs->OpenFile(name);
            if (!inner) {
                ctx.diags.Report(Diag{Severity::Warning, "obxpak.entry.open-failed",
                                       "could not open inner container: " + name,
                                       std::nullopt});
                continue;
            }

            const uint32_t fileIndex = uint32_t(ctx.fileTable->size());
            Onyx::Vfs::IFile& innerRef = *inner;
            ctx.fileTable->push_back(std::shared_ptr<Onyx::Vfs::IFile>(std::move(inner)));

            Onyx::Domain::AssetEntry subtree;
            subtree.name = name;

            std::vector<Onyx::Domain::AssetEntry> children;
            if (!ParseObxToc(innerRef, fileIndex, ctx.diags, children, *toc)) {
                subtree.flags = Onyx::Domain::NodeFlags::Failed;
            }
            subtree.children = std::move(children);
            ctx.roots.push_back(std::move(subtree));
        }

        ctx.state = toc;
        return ParseResult{true};
    }

    // Plain flat .obx (no mount): parse the container file directly, its
    // entries addressed at fileIndex 0 -- the file table's pre-seeded root
    // slot, which is this same file.
    std::vector<Onyx::Domain::AssetEntry> entries;
    if (!ParseObxToc(ctx.file, /*fileIndex=*/0, ctx.diags, entries, *toc)) {
        return ParseResult{false};
    }

    ctx.roots = std::move(entries);
    ctx.state = toc;
    return ParseResult{true};
}

std::unique_ptr<Onyx::Parsers::TextureData> OnyxBoxModule::DecodeImage(DecodeContext& ctx) {
    const uint32_t fileIndex = ctx.entry.source.fileIndex;
    const TocEntry* te = FindTocEntry(ctx.doc, ctx.entry.name, fileIndex);
    if (!te) {
        ctx.diags.Report(Diag{Severity::Error, "obx.image.no-toc-entry",
                               "no TOC entry for " + ctx.entry.name, std::nullopt});
        return nullptr;
    }
    if (fileIndex >= ctx.doc.fileTable.size() || !ctx.doc.fileTable[fileIndex]) return nullptr;

    Onyx::Vfs::IFile& file = *ctx.doc.fileTable[fileIndex];
    const uint64_t fileSize = file.Size();

    // The entry's own declared range must fit in the file, and must be at
    // least large enough to hold the width/height header.
    if (uint64_t(te->payloadOffset) + uint64_t(te->payloadSize) > fileSize ||
        te->payloadSize < 4) {
        ctx.diags.Report(Diag{Severity::Error, "obx.image.range",
                               "image payload out of bounds", std::nullopt});
        return nullptr;
    }

    file.Seek(int64_t(te->payloadOffset), SEEK_SET);
    uint16_t width = 0, height = 0;
    if (!ReadU16LE(file, width) || !ReadU16LE(file, height)) {
        ctx.diags.Report(Diag{Severity::Error, "obx.image.truncated",
                               "image header truncated", std::nullopt});
        return nullptr;
    }

    // Never trust width/height to size the pixel read until they are
    // checked against the TOC's own declared payloadSize: a lying header
    // (e.g. claiming 8x8 inside a 10-byte payload) must not be allowed to
    // read past this entry's declared range into a neighboring entry's
    // bytes -- that range was only verified against the *file*, not
    // against what this specific entry says it owns.
    const uint64_t declaredPixelBytes = uint64_t(width) * uint64_t(height) * 4;
    const uint64_t expectedSize = 4 + declaredPixelBytes;
    if (expectedSize != uint64_t(te->payloadSize)) {
        ctx.diags.Report(Diag{Severity::Error, "obx.image.size-mismatch",
                               ctx.entry.name + ": declared " + std::to_string(width) +
                                   "x" + std::to_string(height) + " exceeds payload",
                               std::nullopt});
        return nullptr;
    }

    auto tex = std::make_unique<Onyx::Parsers::TextureData>();
    tex->name = ctx.entry.name;
    tex->width = width;
    tex->height = height;
    tex->pixels.resize(declaredPixelBytes);
    if (declaredPixelBytes > 0 &&
        file.Read(tex->pixels.data(), declaredPixelBytes) != declaredPixelBytes) {
        ctx.diags.Report(Diag{Severity::Error, "obx.image.truncated",
                               "pixel data truncated", std::nullopt});
        return nullptr;
    }
    return tex;
}

std::optional<TextOut> OnyxBoxModule::DecodeText(DecodeContext& ctx) {
    const uint32_t fileIndex = ctx.entry.source.fileIndex;
    const TocEntry* te = FindTocEntry(ctx.doc, ctx.entry.name, fileIndex);
    if (!te) {
        ctx.diags.Report(Diag{Severity::Error, "obx.text.no-toc-entry",
                               "no TOC entry for " + ctx.entry.name, std::nullopt});
        return std::nullopt;
    }
    if (fileIndex >= ctx.doc.fileTable.size() || !ctx.doc.fileTable[fileIndex]) return std::nullopt;

    Onyx::Vfs::IFile& file = *ctx.doc.fileTable[fileIndex];
    const uint64_t fileSize = file.Size();

    if (uint64_t(te->payloadOffset) + uint64_t(te->payloadSize) > fileSize) {
        ctx.diags.Report(Diag{Severity::Error, "obx.text.range",
                               "text payload out of bounds", std::nullopt});
        return std::nullopt;
    }

    file.Seek(int64_t(te->payloadOffset), SEEK_SET);
    std::string text(te->payloadSize, '\0');
    if (te->payloadSize > 0 && file.Read(text.data(), te->payloadSize) != te->payloadSize) {
        ctx.diags.Report(Diag{Severity::Error, "obx.text.truncated",
                               "text payload truncated", std::nullopt});
        return std::nullopt;
    }

    return TextOut{text, ""};
}

std::unique_ptr<Onyx::Parsers::SceneData> OnyxBoxModule::DecodeMesh(DecodeContext& ctx) {
    const uint32_t fileIndex = ctx.entry.source.fileIndex;
    const TocEntry* te = FindTocEntry(ctx.doc, ctx.entry.name, fileIndex);
    if (!te) {
        ctx.diags.Report(Diag{Severity::Error, "obx.mesh.no-toc-entry",
                               "no TOC entry for " + ctx.entry.name, std::nullopt});
        return nullptr;
    }
    if (fileIndex >= ctx.doc.fileTable.size() || !ctx.doc.fileTable[fileIndex]) return nullptr;

    Onyx::Vfs::IFile& file = *ctx.doc.fileTable[fileIndex];
    const uint64_t fileSize = file.Size();

    // Same hardening ParseObxToc already applied at parse time (which would
    // have flagged this entry Failed and kept CmdDecode/CmdRender from ever
    // reaching here through the normal tree-walk path) -- re-checked here
    // too since a decoder must never trust its caller to have gone through
    // that exact path (spec sec7.1: a decoder is its own last line of
    // defense against a hostile/malformed payload).
    if (uint64_t(te->payloadOffset) + uint64_t(te->payloadSize) > fileSize ||
        te->payloadSize != kMeshPayloadSize) {
        ctx.diags.Report(Diag{Severity::Error, "obx.mesh.size",
                               ctx.entry.name + ": mesh payload must be exactly " +
                                   std::to_string(kMeshPayloadSize) + " bytes (12 floats), got " +
                                   std::to_string(te->payloadSize),
                               std::nullopt});
        return nullptr;
    }

    file.Seek(int64_t(te->payloadOffset), SEEK_SET);
    float values[12];
    for (float& v : values) {
        if (!ReadF32LE(file, v)) {
            ctx.diags.Report(Diag{Severity::Error, "obx.mesh.truncated",
                                   "mesh payload truncated", std::nullopt});
            return nullptr;
        }
    }

    // values[0..3] = baseColor RGBA, [4..6] = position xyz, [7..9] =
    // half-extents xyz, [10..11] = padding (unused). Half-extents are
    // clamped to their absolute value -- a negative half-extent has no
    // sane geometric meaning and would otherwise flip the box inside out.
    const glm::vec3 center(values[4], values[5], values[6]);
    const glm::vec3 halfExtents(std::fabs(values[7]), std::fabs(values[8]), std::fabs(values[9]));

    auto scene = std::make_unique<Onyx::Parsers::SceneData>();
    // Synthetic format, not a real game's legacy axis convention -- no
    // scale(1,1,-1) correction needed (mirrors Tools/OnyxOracle/
    // CorpusScenes.cpp's synthetic scenes, which set this false for the
    // same reason).
    scene->flipZ = false;
    scene->meshParts.push_back(BuildCubePart(center, halfExtents));

    Onyx::Parsers::MaterialDesc mat;
    mat.baseColor[0] = values[0];
    mat.baseColor[1] = values[1];
    mat.baseColor[2] = values[2];
    mat.baseColor[3] = values[3];
    scene->materials.push_back(mat);

    return scene;
}

} // namespace OnyxBox
