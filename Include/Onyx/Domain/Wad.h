#pragma once

// `AssetContainer` aggregates everything an opened WAD exposes to the rest of
// the toolkit: source filename, file handle, and the flat list of parsed
// entries. M1.T1 split this out of the legacy `core/WadTypes.h` umbrella so
// consumers can depend on `Wad.h` directly without pulling the rest of the
// surface. M3b Task 6 dropped the `profile` field: IAssetProfile (the
// profile-era loading layer) is gone -- documents open through GameModules
// and the Workspace now. AssetContainer survives as the generic per-entry
// type ViewerRegistry/ITypeHandler still pass around for viewer dispatch.

#include <memory>
#include <string>
#include <vector>

#include <Onyx/Domain/Entry.h>

namespace Onyx::Vfs {
    class IFile;
}

namespace Onyx::Domain {

struct AssetContainer {
    std::string                        filename;
    std::string                        fullPath;
    std::shared_ptr<Onyx::Vfs::IFile>   fileSource;
    std::vector<AssetEntry>           entries;
};

} // namespace Onyx::Domain
