#include <Onyx/Types/TypeRegistry.h>
#include <Onyx/Services/Logger.h>

namespace Onyx::Types {

TypeRegistry& TypeRegistry::Get() {
    static TypeRegistry instance;
    return instance;
}

void TypeRegistry::RegisterByTypeId(std::unique_ptr<ITypeHandler> handler) {
    if (!handler) return;

    std::lock_guard<std::mutex> lock(m_mutex);

    ITypeHandler* raw = handler.get();
    m_owned.push_back(std::move(handler));
    m_allHandlers.push_back(raw);

    // Deliberately NOT indexing here: raw->GetId() is still 0 at static-init
    // time, before the app seeds the TypeCatalog. See Resolve().
    m_indexDirty = true;

    LOG_INFO("[TypeRegistry] Registered handler: %s", raw->GetName());
}

void TypeRegistry::InvalidateIndex() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_indexDirty = true;
}

void TypeRegistry::RebuildIndexLocked() const {
    m_idMap.clear();
    m_idMap.reserve(m_allHandlers.size());

    for (ITypeHandler* h : m_allHandlers) {
        const uint32_t idKey = h->GetId().value;

        // id 0 is the Unknown sentinel: a handler still reporting it means the
        // catalog was never seeded, so there is nothing useful to index.
        if (idKey == 0) {
            LOG_WARN("[TypeRegistry] Handler '%s' has no TypeId (catalog not seeded?)",
                     h->GetName());
            continue;
        }

        auto [it, inserted] = m_idMap.emplace(idKey, h);
        if (!inserted) {
            LOG_WARN("[TypeRegistry] TypeId=%u claimed by both '%s' and '%s'; keeping '%s'",
                     idKey, it->second->GetName(), h->GetName(), it->second->GetName());
            continue;
        }

        LOG_DEBUG("[TypeRegistry] Indexed %s -> TypeId=%u", h->GetName(), idKey);
    }

    m_indexDirty = false;
    LOG_INFO("[TypeRegistry] Indexed %zu of %zu handlers by TypeId",
             m_idMap.size(), m_allHandlers.size());
}

ITypeHandler* TypeRegistry::Resolve(TypeId id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_indexDirty) RebuildIndexLocked();

    auto it = m_idMap.find(id.value);
    return (it != m_idMap.end()) ? it->second : nullptr;
}

} // namespace Onyx::Types
