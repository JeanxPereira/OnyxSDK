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
//
// All multi-byte integers are little-endian. The TOC walk is defensive: any
// entry whose payload range [payloadOffset, payloadOffset+payloadSize)
// exceeds the file is still added to the tree, but flagged
// Domain::NodeFlags::Failed with a diag ("obx.entry.range") — the walk
// continues (salvage) rather than aborting the whole container. A TOC
// record itself running past the header/EOF region stops the walk cleanly
// with a diag; it never reads out of bounds.

#include <Onyx/Modules/GameModule.h>
#include <Onyx/Modules/DecoderRegistry.h>
#include <Onyx/Types/TypeId.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace OnyxBox {

// One parsed table-of-contents record. Stashed as the document's
// ModuleState (a std::vector<TocEntry>) so decoders can find a payload's
// range by name without re-walking the file.
struct TocEntry {
    std::string name;
    uint8_t     kind = 0;          // 0=blob, 1=image, 2=text
    uint32_t    payloadOffset = 0;
    uint32_t    payloadSize = 0;
};

class OnyxBoxModule final : public Onyx::Modules::IGameModule {
public:
    Onyx::Modules::ModuleInfo  Info() const override;
    Onyx::Modules::ProbeResult Probe(const Onyx::Modules::ProbeInput&) const override;
    void RegisterTypes(Onyx::Types::TypeRegistrar&) override;
    void RegisterDecoders(Onyx::Modules::DecoderRegistry&) override;
    Onyx::Modules::ParseResult ParseContainer(Onyx::Modules::ContainerContext&) override;

private:
    static std::unique_ptr<Onyx::Parsers::TextureData>
        DecodeImage(Onyx::Modules::DecodeContext&);
    static std::optional<Onyx::Modules::TextOut>
        DecodeText(Onyx::Modules::DecodeContext&);

    Onyx::Types::TypeId m_imageType;
    Onyx::Types::TypeId m_textType;
    Onyx::Types::TypeId m_blobType;
};

} // namespace OnyxBox
