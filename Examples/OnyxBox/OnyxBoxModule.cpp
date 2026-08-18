#include "OnyxBoxModule.h"

#include <Onyx/Modules/Workspace.h>
#include <Onyx/Vfs/IFile.h>

#include <algorithm>
#include <cctype>
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

// Looks up the parsed TOC entry matching `name`, stashed as ModuleState by
// ParseContainer -- the decoder never re-walks the file to find it.
const TocEntry* FindTocEntry(Document& doc, const std::string& name) {
    if (!doc.state) return nullptr;
    auto* toc = static_cast<std::vector<TocEntry>*>(doc.state.get());
    for (const auto& e : *toc) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

} // namespace

ModuleInfo OnyxBoxModule::Info() const {
    return ModuleInfo{
        "obx",
        "OnyxBox (example)",
        {"obx"},
        {OpenFilter{"OnyxBox", {"obx"}}},
    };
}

ProbeResult OnyxBoxModule::Probe(const ProbeInput& in) const {
    if (in.header.size() >= 4 &&
        std::memcmp(in.header.data(), "OBX1", 4) == 0) {
        return ProbeResult{95, "OBX1 magic at 0"};
    }

    std::string ext = in.path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](unsigned char c) { return char(std::tolower(c)); });
    if (ext == ".obx") {
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
}

void OnyxBoxModule::RegisterDecoders(DecoderRegistry& reg) {
    reg.Image(m_imageType, &OnyxBoxModule::DecodeImage);
    reg.Text(m_textType, &OnyxBoxModule::DecodeText);
}

ParseResult OnyxBoxModule::ParseContainer(ContainerContext& ctx) {
    Onyx::Vfs::IFile& file = ctx.file;

    file.Seek(0, SEEK_SET);
    char magic[4] = {};
    if (file.Read(magic, sizeof(magic)) != sizeof(magic) ||
        std::memcmp(magic, "OBX1", 4) != 0) {
        ctx.diags.Report(Diag{Severity::Error, "obx.header.bad",
                               "missing OBX1 magic", std::nullopt});
        return ParseResult{false};
    }

    uint32_t count = 0;
    if (!ReadU32LE(file, count)) {
        ctx.diags.Report(Diag{Severity::Error, "obx.header.truncated",
                               "file too small for TOC count", std::nullopt});
        return ParseResult{false};
    }

    std::vector<TocEntry> toc;
    toc.reserve(count);

    const uint64_t fileSize = file.Size();

    for (uint32_t i = 0; i < count; ++i) {
        uint16_t nameLen = 0;
        if (!ReadU16LE(file, nameLen)) {
            ctx.diags.Report(Diag{Severity::Warning, "obx.toc.truncated",
                                   "TOC entry " + std::to_string(i) +
                                       " truncated reading nameLen",
                                   std::nullopt});
            break;
        }

        std::string name(nameLen, '\0');
        if (nameLen > 0 && file.Read(name.data(), nameLen) != nameLen) {
            ctx.diags.Report(Diag{Severity::Warning, "obx.toc.truncated",
                                   "TOC entry " + std::to_string(i) +
                                       " truncated reading name",
                                   std::nullopt});
            break;
        }

        uint8_t kind = 0;
        if (file.Read(&kind, 1) != 1) {
            ctx.diags.Report(Diag{Severity::Warning, "obx.toc.truncated",
                                   "TOC entry " + std::to_string(i) +
                                       " truncated reading kind",
                                   std::nullopt});
            break;
        }

        uint32_t payloadOffset = 0, payloadSize = 0;
        if (!ReadU32LE(file, payloadOffset) || !ReadU32LE(file, payloadSize)) {
            ctx.diags.Report(Diag{Severity::Warning, "obx.toc.truncated",
                                   "TOC entry " + std::to_string(i) +
                                       " truncated reading payload range",
                                   std::nullopt});
            break;
        }

        toc.push_back(TocEntry{name, kind, payloadOffset, payloadSize});

        Onyx::Domain::AssetEntry entry;
        entry.name = name;
        entry.offset = payloadOffset;
        entry.size = payloadSize;

        switch (kind) {
            case 1:
                entry.typeId = m_imageType;
                entry.kind = Onyx::Domain::MediaKind::Image;
                break;
            case 2:
                entry.typeId = m_textType;
                entry.kind = Onyx::Domain::MediaKind::Unknown;
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
            ctx.diags.Report(Diag{Severity::Error, "obx.entry.range",
                                   "payload out of bounds", std::nullopt});
        }

        ctx.roots.push_back(std::move(entry));
    }

    ctx.state = std::make_shared<std::vector<TocEntry>>(std::move(toc));
    return ParseResult{true};
}

std::unique_ptr<Onyx::Parsers::TextureData> OnyxBoxModule::DecodeImage(DecodeContext& ctx) {
    const TocEntry* te = FindTocEntry(ctx.doc, ctx.entry.name);
    if (!te) {
        ctx.diags.Report(Diag{Severity::Error, "obx.image.no-toc-entry",
                               "no TOC entry for " + ctx.entry.name, std::nullopt});
        return nullptr;
    }
    if (!ctx.doc.file) return nullptr;

    Onyx::Vfs::IFile& file = *ctx.doc.file;
    const uint64_t fileSize = file.Size();

    if (uint64_t(te->payloadOffset) + 4 > fileSize) {
        ctx.diags.Report(Diag{Severity::Error, "obx.image.range",
                               "image header out of bounds", std::nullopt});
        return nullptr;
    }

    file.Seek(int64_t(te->payloadOffset), SEEK_SET);
    uint16_t width = 0, height = 0;
    if (!ReadU16LE(file, width) || !ReadU16LE(file, height)) {
        ctx.diags.Report(Diag{Severity::Error, "obx.image.truncated",
                               "image header truncated", std::nullopt});
        return nullptr;
    }

    const uint64_t pixelBytes = uint64_t(width) * uint64_t(height) * 4;
    if (uint64_t(te->payloadOffset) + 4 + pixelBytes > fileSize) {
        ctx.diags.Report(Diag{Severity::Error, "obx.image.range",
                               "pixel data out of bounds", std::nullopt});
        return nullptr;
    }

    auto tex = std::make_unique<Onyx::Parsers::TextureData>();
    tex->name = ctx.entry.name;
    tex->width = width;
    tex->height = height;
    tex->pixels.resize(pixelBytes);
    if (pixelBytes > 0 && file.Read(tex->pixels.data(), pixelBytes) != pixelBytes) {
        ctx.diags.Report(Diag{Severity::Error, "obx.image.truncated",
                               "pixel data truncated", std::nullopt});
        return nullptr;
    }
    return tex;
}

std::optional<TextOut> OnyxBoxModule::DecodeText(DecodeContext& ctx) {
    const TocEntry* te = FindTocEntry(ctx.doc, ctx.entry.name);
    if (!te) {
        ctx.diags.Report(Diag{Severity::Error, "obx.text.no-toc-entry",
                               "no TOC entry for " + ctx.entry.name, std::nullopt});
        return std::nullopt;
    }
    if (!ctx.doc.file) return std::nullopt;

    Onyx::Vfs::IFile& file = *ctx.doc.file;
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

} // namespace OnyxBox
