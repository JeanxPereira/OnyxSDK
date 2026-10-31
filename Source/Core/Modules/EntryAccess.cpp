#include <Onyx/Core/Modules/EntryAccess.h>

#include <Onyx/Core/Modules/DecodeContext.h>
#include <Onyx/Core/Modules/DecoderRegistry.h>

namespace Onyx::Modules {

std::shared_ptr<const Vfs::IFile> FileOf(const Document& doc, const Domain::ByteRange& range) {
    return range.fileIndex < doc.files.size() ? doc.files[range.fileIndex] : nullptr;
}

std::optional<Vfs::Reader> ReaderOf(const Document& doc, const Domain::AssetEntry& entry) {
    std::shared_ptr<const Vfs::IFile> file = FileOf(doc, entry.source);
    if (!file) return std::nullopt;
    return Vfs::Reader(std::move(file), entry.source.offset, entry.source.size);
}

ScopedDocument::ScopedDocument(Workspace& ws, const std::filesystem::path& path, std::string_view moduleHint)
    : m_ws(ws) {
    m_id = ws.Open(path, moduleHint);
    if (m_id != 0) m_doc = ws.Get(m_id);
}

ScopedDocument::~ScopedDocument() {
    if (m_id != 0) m_ws.Close(m_id);
}

std::unique_ptr<Domain::Scene> DecodeSceneOf(Workspace& ws, Document& doc, const Domain::AssetEntry& entry,
                                             Services::Progress& progress) {
    std::optional<Vfs::Reader> reader = ReaderOf(doc, entry);
    if (!reader) return nullptr;
    DecodeContext ctx{&doc, entry, std::move(*reader), doc.diags, progress, ws.Settings(), doc.moduleState};
    return ws.Decoders().DecodeScene(ctx);
}

}
