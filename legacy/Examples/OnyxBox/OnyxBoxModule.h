#pragma once

// OnyxBox — a synthetic example module (id "obx") proving the v1 module
// contracts (IGameModule, Workspace, DecoderRegistry, TypeRegistrar) end to
// end without depending on any real game format.
//
// ── The OBX1 container format (invented for this example) ─────────────────
//
//   offset 0: magic "OBX1"                 (4 bytes)
//   offset 4: u32 count                    (little-endian)
//   then `count` TOC entries, packed back to back:
//       u16 nameLen | name bytes (UTF-8, nameLen bytes) | u8 kind
//       u32 payloadOffset (absolute, from file start) | u32 payloadSize
//     kind: 0 = blob, 1 = image, 2 = text
//
//   Payload layouts (found at [payloadOffset, payloadOffset+payloadSize)):
//     image: u16 width | u16 height | width*height*4 raw RGBA8 bytes
//     text:  raw UTF-8 bytes (no terminator)
//     blob:  opaque bytes, not decoded
//     mesh:  12 little-endian f32 (48 bytes, exactly -- see the Task 14
//            hardening note below): baseColor RGBA (4) | position xyz (3) |
//            half-extents xyz (3) | 2 unused padding floats. Decodes to a
//            single-part axis-aligned cube (Parsers::SceneData, one
//            MeshPart of 24 vertices/6 faces with outward per-face normals,
//            one MaterialDesc at index 0 carrying baseColor) -- the
//            minimal honest Scene capability this synthetic format needs
//            to exercise the M4 CLI Scene branch and `render` command
//            without depending on any real game format's mesh layout.
//
// All multi-byte integers are little-endian. The TOC walk is defensive: any
// entry whose payload range [payloadOffset, payloadOffset+payloadSize)
// exceeds the file is still added to the tree, but flagged
// Domain::NodeFlags::Failed with a diag ("obx.entry.range") — the walk
// continues (salvage) rather than aborting the whole container. A TOC
// record itself running past the header/EOF region stops the walk cleanly
// with a diag; it never reads out of bounds.
//
// ── The OBXPAK format (Task 8) ─────────────────────────────────────────────
//
//   offset 0: magic "OBP1"                 (4 bytes)
//   offset 4: u32 count                    (little-endian)
//   then `count` files, packed back to back:
//       u32 nameLen | name bytes (UTF-8, nameLen bytes)
//       u32 size | raw bytes of a complete OBX1 container (`size` bytes)
//
// A .obxpak mounts as an in-memory VFS (MountSpec{"OnyxBox pak", {"obxpak"}})
// whose ListDirectory returns each inner file's name and OpenFile hands back
// an IFile view over its OBX1 bytes within the pak. ParseContainer, when
// mounted, opens every inner file through that VFS, pushes it into the
// Document's file table, and parses it with the same OBX1 TOC walker as a
// flat .obx -- into its own child subtree named after the inner file, with
// every resulting entry's ByteRange::fileIndex stamped to that file's table
// slot (so extract and the decoders read the right file's bytes, never the
// pak's own bytes or a same-named entry from a different inner file).

#include <Onyx/Modules/GameModule.h>
#include <Onyx/Modules/DecoderRegistry.h>
#include <Onyx/Services/Diagnostics.h>
#include <Onyx/Types/TypeId.h>
#include <Onyx/Vfs/IFile.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace OnyxBox {

// One parsed table-of-contents record. Stashed as the document's
// ModuleState (a std::vector<TocEntry>) so decoders can find a payload's
// range by name without re-walking the file. `fileIndex` disambiguates
// entries of the same name coming from different inner OBX files under an
// obxpak mount (see FindTocEntry in the .cpp).
struct TocEntry {
    std::string name;
    uint8_t     kind = 0;          // 0=blob, 1=image, 2=text, 3=mesh
    uint32_t    payloadOffset = 0;
    uint32_t    payloadSize = 0;
    uint32_t    fileIndex = 0;     // slot into the Document's file table
};

class OnyxBoxModule final : public Onyx::Modules::IGameModule {
public:
    Onyx::Modules::ModuleInfo  Info() const override;
    Onyx::Modules::ProbeResult Probe(const Onyx::Modules::ProbeInput&) const override;
    void RegisterTypes(Onyx::Types::TypeRegistrar&) override;
    void RegisterDecoders(Onyx::Modules::DecoderRegistry&) override;
    std::vector<Onyx::Modules::MountSpec> Mounts() const override;
    Onyx::Modules::ParseResult ParseContainer(Onyx::Modules::ContainerContext&) override;

private:
    static std::unique_ptr<Onyx::Parsers::TextureData>
        DecodeImage(Onyx::Modules::DecodeContext&);
    static std::optional<Onyx::Modules::DecodedText>
        DecodeText(Onyx::Modules::DecodeContext&);
    // kind=3 (mesh) -- see this file's top comment for the payload layout
    // and the single-part cube it decodes to.
    static std::unique_ptr<Onyx::Parsers::SceneData>
        DecodeMesh(Onyx::Modules::DecodeContext&);

    // Walks one OBX1 container's TOC starting at `file`'s current position
    // 0, appending every entry it finds to outEntries/outToc (stamped with
    // `fileIndex`). Returns false only for a header-level failure (bad
    // magic, or the file too short to hold the TOC count) -- a truncated
    // individual TOC entry stops the walk (salvage: keep what parsed) but
    // still returns true. Shared by the flat .obx path (fileIndex 0, the
    // container file itself) and the obxpak mount path (one call per inner
    // file, fileIndex = that file's file-table slot).
    bool ParseObxToc(Onyx::Vfs::IFile& file, uint32_t fileIndex,
                      Onyx::Services::DiagSink& diags,
                      std::vector<Onyx::Domain::AssetEntry>& outEntries,
                      std::vector<TocEntry>& outToc);

    Onyx::Types::TypeId m_imageType;
    Onyx::Types::TypeId m_textType;
    Onyx::Types::TypeId m_blobType;
    Onyx::Types::TypeId m_meshType;
};

} // namespace OnyxBox
