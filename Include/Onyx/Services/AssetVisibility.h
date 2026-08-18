#pragma once
#include <Onyx/Types/TypeId.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Onyx::Domain { struct AssetEntry; }
using AssetEntry = Onyx::Domain::AssetEntry;

namespace Onyx::Types { class TypeCatalog; }

namespace Onyx::Services {

class Settings;

/// Controls whether a type appears in the asset browser tree.
enum class Visibility : uint8_t {
    Visible,   // Always shown
    Hidden,    // Hidden by default, user can toggle on
    Internal   // Never shown (structural)
};

/// Centralized, data-driven asset visibility registry, keyed by TypeId.
/// Each Types::TypeId has a default Visibility; users can override
/// Hidden<->Visible via the Filters panel. Overrides persist by type key in
/// the TOML config. The store holds no game knowledge; app-level code seeds
/// defaults at startup.
class AssetVisibility {
public:
    static AssetVisibility& Get();

    bool IsVisible(Types::TypeId id) const;
    bool IsVisible(const AssetEntry& entry) const;

    Visibility GetDefault(Types::TypeId id) const;
    Visibility GetCurrent(Types::TypeId id) const;

    void SetDefault(Types::TypeId id, Visibility vis);

    void SetUserOverride(Types::TypeId id, bool visible);
    void ClearUserOverride(Types::TypeId id);
    void ResetAllOverrides();

    struct TypeVisInfo {
        Types::TypeId id;
        const char*   name;
        const char*   icon;
        Visibility    defaultVis;
        bool          hasOverride;
        bool          currentlyVisible;
    };

    std::vector<TypeVisInfo> GetFilterableTypes() const;

    // Overrides persist by type key ("gowr.mesh"), not numeric TypeId, so a
    // catalog re-registering types in a different order (or an app adding
    // new types) never corrupts previously saved user overrides. A key that
    // no longer resolves to a registered type (removed/renamed) is silently
    // dropped on load rather than resurrected as a garbage override.
    // Not thread-safe: confine an instance to one thread or guard it
    // externally.
    void SaveOverrides(const Types::TypeCatalog& catalog, Settings& into) const;   // writes "visibility.<key>" = bool
    // Not thread-safe: confine an instance to one thread or guard it
    // externally.
    void LoadOverrides(const Types::TypeCatalog& catalog, const Settings& from);   // unknown keys are dropped

    std::vector<std::pair<std::string, bool>> ExportOverridesByKey(const Types::TypeCatalog& catalog) const;

private:
    AssetVisibility();

    std::unordered_map<uint32_t, Visibility> m_defaults;  // key = TypeId.value
    std::unordered_map<uint32_t, bool>       m_overrides; // key = TypeId.value
};

} // namespace Onyx::Services
