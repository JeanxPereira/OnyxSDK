#pragma once
#include <Onyx/Types/TypeCatalog.h>
#include <string>

namespace Onyx::Types {

// A registrar bound to one module id. Every key it mints is prefixed
// "<moduleId>." — cross-module collisions are impossible by construction.
class TypeRegistrar {
public:
    TypeRegistrar(TypeCatalog& catalog, std::string moduleId);

    // spec.key must be bare ("mesh", not "gowr.mesh"): a key containing
    // '.' is refused (returns invalid TypeId and logs). Re-adding the
    // same bare key returns the existing handle.
    TypeId Add(const TypeInfo& spec);

    const std::string& ModuleId() const;

private:
    TypeCatalog& m_catalog;
    std::string m_moduleId;
};

} // namespace Onyx::Types
