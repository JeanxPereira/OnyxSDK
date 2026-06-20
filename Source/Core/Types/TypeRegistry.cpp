#include <Onyx/Types/TypeRegistry.h>
#include <Onyx/Services/Logger.h>

namespace Onyx::Types {

TypeRegistry& TypeRegistry::Get() {
    static TypeRegistry instance;
    return instance;
}

void TypeRegistry::RegisterByTypeId(std::unique_ptr<ITypeHandler> handler) {
    if (!handler) return;

    ITypeHandler* raw = handler.get();
    m_owned.push_back(std::move(handler));
    m_allHandlers.push_back(raw);

    uint32_t idKey = raw->GetId().value;
    m_idMap[idKey] = raw;  // one canonical handler per TypeId

    LOG_INFO("[TypeRegistry] Registered handler: %s (TypeId=%d)", raw->GetName(), idKey);
}

ITypeHandler* TypeRegistry::Resolve(TypeId id) const {
    auto it = m_idMap.find(id.value);
    return (it != m_idMap.end()) ? it->second : nullptr;
}

} // namespace Onyx::Types
