#pragma once

// Domain types describing a single entry inside an opened WAD.
//
// `AssetEntry` is the tree node profiles produce after they walk a WAD.
// M1.T1 extracted it out of the monolithic `core/WadTypes.h` umbrella
// so consumers can depend on it in isolation. The umbrella is still
// around for transitional reasons.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <Onyx/Domain/MediaKind.h>
#include <Onyx/Domain/ProfileTag.h>
#include <Onyx/Schema/AssetNode.h>
#include <Onyx/Types/TypeId.h>

namespace Onyx::Domain {

// â”€â”€ AssetEntry â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// One node in the parsed WAD tree. Profiles populate this; the inspector
// UI and the test snapshot harness both read it.
struct AssetEntry {
    std::string           name;
    std::string           wadName;
    uint32_t              size = 0;
    uint32_t              offset = 0;
    uint64_t              hash = 0;

    // Compiled type identifier
    Onyx::Types::TypeId    typeId = {};

    // Child nodes for UI tree
    std::vector<AssetEntry> children;

    // Loaded data (on demand)
    std::shared_ptr<Onyx::Schema::AssetNode> assetNode;

    MediaKind kind       = MediaKind::Unknown;
    ProfileTag profileTag;
    std::string   displayName;   // human-friendly name (falls back to name if empty)
};

} // namespace Onyx::Domain

// Backwards-compat alias â€” keep existing call sites compiling at global scope.
using AssetEntry    = Onyx::Domain::AssetEntry;
