#include <Onyx/Core/Types/TypeCatalog.h>

namespace Onyx::Types {

TypeCatalog::TypeCatalog() {
    TypeSpec unknown;
    unknown.key = "onyx.unknown";
    unknown.label = "Unknown";
    unknown.media = Domain::MediaKind::Unknown;
    unknown.icon = IconId::None;
    RegisterLocked(unknown);

    TypeSpec folder;
    folder.key = "onyx.folder";
    folder.label = "Folder";
    folder.media = Domain::MediaKind::Container;
    folder.icon = IconId::Folder;
    RegisterLocked(folder);

    TypeSpec raw;
    raw.key = "onyx.raw";
    raw.label = "Raw";
    raw.media = Domain::MediaKind::Raw;
    raw.icon = IconId::Raw;
    RegisterLocked(raw);
}

TypeId TypeCatalog::RegisterLocked(const TypeSpec& fullSpec) {
    if (auto it = m_byKey.find(fullSpec.key); it != m_byKey.end()) return it->second;
    const TypeId id{static_cast<uint32_t>(m_specs.size())};
    m_specs.push_back(fullSpec);
    m_byKey.emplace(fullSpec.key, id);
    return id;
}

TypeId TypeCatalog::Register(std::string_view moduleId, const TypeSpec& spec) {
    TypeSpec full = spec;
    full.key.clear();
    full.key.reserve(moduleId.size() + 1 + spec.key.size());
    full.key.append(moduleId).append(".").append(spec.key);
    std::lock_guard lock(m_mutex);
    return RegisterLocked(full);
}

TypeId TypeCatalog::Find(std::string_view fullKey) const {
    std::lock_guard lock(m_mutex);
    auto it = m_byKey.find(std::string(fullKey));
    return it == m_byKey.end() ? TypeId{} : it->second;
}

const TypeSpec& TypeCatalog::Spec(TypeId id) const {
    std::lock_guard lock(m_mutex);
    return id.value < m_specs.size() ? m_specs[id.value] : m_specs.front();
}

Domain::MediaKind TypeCatalog::Media(TypeId id) const {
    return Spec(id).media;
}

std::string_view TypeCatalog::Key(TypeId id) const {
    std::lock_guard lock(m_mutex);
    if (id.value >= m_specs.size()) return {};
    return m_specs[id.value].key;
}

size_t TypeCatalog::Count() const {
    std::lock_guard lock(m_mutex);
    return m_specs.size();
}

std::vector<TypeId> TypeCatalog::All() const {
    std::lock_guard lock(m_mutex);
    std::vector<TypeId> ids;
    ids.reserve(m_specs.size());
    for (size_t i = 0; i < m_specs.size(); ++i) ids.push_back(TypeId{static_cast<uint32_t>(i)});
    return ids;
}

}
