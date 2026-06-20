#pragma once
#include <Onyx/Types/TypeId.h>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Onyx::Domain { struct AssetEntry; }
using AssetEntry = Onyx::Domain::AssetEntry;

namespace Onyx::Services {

/// Controls whether a type appears in the asset browser tree.
enum class Visibility : uint8_t {
    Visible,   // Always shown
    Hidden,    // Hidden by default, user can toggle on
    Internal   // Never shown (structural)
};

/// Centralized, data-driven asset visibility registry, keyed by TypeId.
/// Each Types::TypeId has a default Visibility; users can override
/// Hidden<->Visible via the Filters panel. Overrides persist in the GTKC config.
/// The store holds no game knowledge; app-level code seeds defaults at startup.
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

    // Overrides serialize as a compact array. key = TypeId value.
    struct SerializedOverride {
        uint16_t typeId;
        uint8_t  visible;   // 0 or 1
        uint8_t  _pad = 0;
    };
    static_assert(sizeof(SerializedOverride) == 4, "Must be 4 bytes for GTKC alignment");

    std::vector<SerializedOverride> ExportOverrides() const;
    void ImportOverrides(const std::vector<SerializedOverride>& data);

private:
    AssetVisibility();

    std::unordered_map<uint32_t, Visibility> m_defaults;  // key = TypeId.value
    std::unordered_map<uint32_t, bool>       m_overrides; // key = TypeId.value
};

} // namespace Onyx::Services
