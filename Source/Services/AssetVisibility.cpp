#include <Onyx/Services/AssetVisibility.h>
#include <Onyx/Domain/Entry.h>
#include <Onyx/Services/Settings.h>
#include <Onyx/Types/TypeCatalog.h>
#include <Onyx/Types/TypeRegistry.h>

namespace Onyx::Services {

AssetVisibility& AssetVisibility::Get() { static AssetVisibility s; return s; }
AssetVisibility::AssetVisibility() = default;

void AssetVisibility::SetDefault(Types::TypeId id, Visibility vis) { m_defaults[id.value] = vis; }

Visibility AssetVisibility::GetDefault(Types::TypeId id) const {
    auto it = m_defaults.find(id.value);
    return (it != m_defaults.end()) ? it->second : Visibility::Visible;
}

Visibility AssetVisibility::GetCurrent(Types::TypeId id) const {
    Visibility def = GetDefault(id);
    if (def == Visibility::Internal) return Visibility::Internal;
    auto it = m_overrides.find(id.value);
    if (it != m_overrides.end()) return it->second ? Visibility::Visible : Visibility::Hidden;
    return def;
}

bool AssetVisibility::IsVisible(Types::TypeId id) const { return GetCurrent(id) == Visibility::Visible; }
bool AssetVisibility::IsVisible(const AssetEntry& entry) const { return IsVisible(entry.typeId); }

void AssetVisibility::SetUserOverride(Types::TypeId id, bool visible) {
    Visibility def = GetDefault(id);
    if (def == Visibility::Internal) return;
    bool defaultVisible = (def == Visibility::Visible);
    if (visible == defaultVisible) m_overrides.erase(id.value);
    else m_overrides[id.value] = visible;
}

void AssetVisibility::ClearUserOverride(Types::TypeId id) { m_overrides.erase(id.value); }
void AssetVisibility::ResetAllOverrides() { m_overrides.clear(); }

std::vector<AssetVisibility::TypeVisInfo> AssetVisibility::GetFilterableTypes() const {
    std::vector<TypeVisInfo> result;
    const auto& handlers = Types::TypeRegistry::Get().AllHandlers();
    for (const auto* handler : handlers) {
        Types::TypeId id = handler->GetId();
        Visibility def = GetDefault(id);
        if (def == Visibility::Internal) continue;
        if (!id.valid()) continue;
        TypeVisInfo info;
        info.id               = id;
        info.name             = handler->GetName();
        info.icon             = handler->GetIcon();
        info.defaultVis       = def;
        info.currentlyVisible = (GetCurrent(id) == Visibility::Visible);
        info.hasOverride      = (m_overrides.find(id.value) != m_overrides.end());
        result.push_back(info);
    }
    return result;
}

std::vector<std::pair<std::string, bool>> AssetVisibility::ExportOverridesByKey(const Types::TypeCatalog& catalog) const {
    std::vector<std::pair<std::string, bool>> result;
    result.reserve(m_overrides.size());
    for (const auto& [idValue, visible] : m_overrides) {
        std::string_view key = catalog.KeyOf(Types::TypeId{idValue});
        if (key.empty()) continue;   // type no longer registered; drop it
        result.emplace_back(std::string(key), visible);
    }
    return result;
}

void AssetVisibility::SaveOverrides(const Types::TypeCatalog& catalog, Settings& into) const {
    for (const auto& [key, visible] : ExportOverridesByKey(catalog)) {
        into.Set("visibility." + key, visible);
    }
}

void AssetVisibility::LoadOverrides(const Types::TypeCatalog& catalog, const Settings& from) {
    m_overrides.clear();
    // Walk every registered type rather than the settings file: a key the
    // catalog never claimed is thereby never looked up, which is how
    // unknown/stale keys get dropped silently.
    for (size_t i = 0; i < catalog.Count(); ++i) {
        Types::TypeId id{(uint32_t)i};
        std::string_view key = catalog.KeyOf(id);
        if (key.empty()) continue;
        if (auto v = from.GetBool("visibility." + std::string(key))) {
            m_overrides[id.value] = *v;
        }
    }
}

} // namespace Onyx::Services
