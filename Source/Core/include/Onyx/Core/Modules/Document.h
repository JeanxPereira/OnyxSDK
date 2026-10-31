#pragma once

#include <Onyx/Core/Domain/AssetEntry.h>
#include <Onyx/Core/Modules/GameModule.h>
#include <Onyx/Core/Services/Diagnostics.h>
#include <Onyx/Core/Services/Jobs.h>
#include <Onyx/Core/Services/Settings.h>
#include <Onyx/Core/Vfs/IFile.h>
#include <Onyx/Core/Vfs/IVirtualFileSystem.h>
#include <Onyx/Core/Vfs/Reader.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace Onyx::Modules {

using DocumentId = uint64_t;

struct ContainerContext {
    Vfs::Reader reader;
    const std::filesystem::path& path;
    Services::Settings& settings;
    Services::DiagSink& diags;
    Services::Progress& progress;
    std::shared_ptr<void>& moduleState;
    std::vector<Domain::AssetEntry>& roots;
    Vfs::IVirtualFileSystem* mounted;
    std::vector<std::shared_ptr<const Vfs::IFile>>& files;
};

// A worker thread owns roots, files, mounted, diags and moduleState from the open call until the
// document's TreeReady event is pumped; only `ready` may be read before that, while `id`, `path`
// and `module` are fixed by Open and may be read at any time. files[0] is always
// the root container file, and AssetEntry::source.fileIndex indexes into files.
struct Document {
    DocumentId id = 0;
    std::filesystem::path path;
    IGameModule* module = nullptr;
    std::vector<Domain::AssetEntry> roots;
    std::vector<std::shared_ptr<const Vfs::IFile>> files;
    std::shared_ptr<Vfs::IVirtualFileSystem> mounted;
    Services::DiagSink diags;
    std::shared_ptr<void> moduleState;
    std::atomic<bool> ready{false};
    Services::JobHandle parseJob;
};

}
