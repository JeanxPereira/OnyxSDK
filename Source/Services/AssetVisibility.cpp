#include <Onyx/Services/AssetVisibility.h>
#include <Onyx/Domain/Entry.h>
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

std::vector<AssetVisibility::SerializedOverride> AssetVisibility::ExportOverrides() const {
    std::vector<SerializedOverride> result;
    result.reserve(m_overrides.size());
    for (const auto& [key, vis] : m_overrides) {
        SerializedOverride so;
        so.typeId  = (uint16_t)key;
        so.visible = vis ? 1 : 0;
        so._pad    = 0;
        result.push_back(so);
    }
    return result;
}

void AssetVisibility::ImportOverrides(const std::vector<SerializedOverride>& data) {
    m_overrides.clear();
    // Legacy GTKC keys packed (gameVer<<8 | typeId): GOW2 (ver 0) records read
    // back unchanged here; GOWR (ver 1) records land on a typeId no live type
    // owns and are harmless orphans (never queried). Acceptable one-time reset.
    for (const auto& so : data) {
        m_overrides[so.typeId] = (so.visible != 0);
    }
}

} // namespace Onyx::Services
