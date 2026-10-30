#pragma once

#include <Onyx/Core/Domain/MediaKind.h>
#include <Onyx/Core/Types/TypeId.h>
#include <Onyx/Core/Types/TypeSpec.h>

#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Onyx::Types {

class TypeCatalog {
public:
    TypeCatalog();

    TypeCatalog(const TypeCatalog&) = delete;
    TypeCatalog& operator=(const TypeCatalog&) = delete;

    TypeId Register(std::string_view moduleId, const TypeSpec& spec);
    TypeId Find(std::string_view fullKey) const;
    const TypeSpec& Spec(TypeId id) const;
    Domain::MediaKind Media(TypeId id) const;
    std::string_view Key(TypeId id) const;
    size_t Count() const;
    std::vector<TypeId> All() const;

private:
    TypeId RegisterLocked(const TypeSpec& fullSpec);

    mutable std::mutex m_mutex;
    std::deque<TypeSpec> m_specs;
    std::unordered_map<std::string, TypeId> m_byKey;
};

}
