#pragma once

#include <Onyx/Core/Domain/AssetEntry.h>
#include <Onyx/Core/Domain/ByteRange.h>
#include <Onyx/Core/Domain/Scene.h>
#include <Onyx/Core/Modules/Document.h>
#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/Core/Services/Jobs.h>
#include <Onyx/Core/Vfs/IFile.h>
#include <Onyx/Core/Vfs/Reader.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

namespace Onyx::Modules {

// Null when `range.fileIndex` names a slot the document does not hold.
std::shared_ptr<const Vfs::IFile> FileOf(const Document& doc, const Domain::ByteRange& range);

// A reader over the entry's bytes; nullopt when FileOf is null.
std::optional<Vfs::Reader> ReaderOf(const Document& doc, const Domain::AssetEntry& entry);

// Opens `path` for the guard's lifetime and closes it on destruction. ok() is false when no module
// accepts it.
class ScopedDocument {
public:
    ScopedDocument(Workspace& ws, const std::filesystem::path& path, std::string_view moduleHint);
    ~ScopedDocument();

    ScopedDocument(const ScopedDocument&) = delete;
    ScopedDocument& operator=(const ScopedDocument&) = delete;

    bool ok() const { return m_doc != nullptr; }
    Document& doc() { return *m_doc; }
    DocumentId id() const { return m_id; }

private:
    Workspace& m_ws;
    DocumentId m_id = 0;
    Document* m_doc = nullptr;
};

// Builds the decode context from the entry's range and the workspace settings, then DecodeScene.
// Null when the entry's file is missing or no scene decoder accepts it.
std::unique_ptr<Domain::Scene> DecodeSceneOf(Workspace& ws, Document& doc, const Domain::AssetEntry& entry,
                                             Services::Progress& progress);

}
