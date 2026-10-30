#pragma once

#include <Onyx/Core/Types/TypeCatalog.h>

#include <string>
#include <string_view>

namespace Onyx::Types {

class TypeRegistrar {
public:
    TypeRegistrar(TypeCatalog& catalog, std::string moduleId);

    TypeId Add(const TypeSpec& spec);
    std::string_view ModuleId() const { return m_moduleId; }

private:
    TypeCatalog& m_catalog;
    std::string m_moduleId;
};

}
