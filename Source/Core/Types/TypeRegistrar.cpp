#include <Onyx/Core/Types/TypeRegistrar.h>

namespace Onyx::Types {

TypeRegistrar::TypeRegistrar(TypeCatalog& catalog, std::string moduleId)
    : m_catalog(catalog), m_moduleId(std::move(moduleId)) {}

TypeId TypeRegistrar::Add(const TypeSpec& spec) {
    if (spec.key.find('.') != std::string::npos) return {};
    return m_catalog.Register(m_moduleId, spec);
}

}
