#include <Onyx/Types/TypeRegistrar.h>
#include <Onyx/Services/Logger.h>

namespace Onyx::Types {

TypeRegistrar::TypeRegistrar(TypeCatalog& catalog, std::string moduleId)
    : m_catalog(catalog), m_moduleId(std::move(moduleId)) {
}

TypeId TypeRegistrar::Add(const TypeInfo& spec) {
    if (spec.key.find('.') != std::string::npos) {
        LOG_ERR("[TypeRegistrar] '%s': bare keys only (got '%s')",
                m_moduleId.c_str(), spec.key.c_str());
        return {};
    }
    TypeInfo namespaced = spec;
    namespaced.key = m_moduleId + "." + spec.key;
    return m_catalog.Register(namespaced);
}

const std::string& TypeRegistrar::ModuleId() const {
    return m_moduleId;
}

} // namespace Onyx::Types
