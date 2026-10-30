#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace Onyx::Types {

struct TypeId {
    uint32_t value = 0;

    bool Valid() const { return value != 0; }
    auto operator<=>(const TypeId&) const = default;
};

struct TypeKey {
    std::string value;

    bool operator==(const TypeKey&) const = default;
};

}

template <>
struct std::hash<Onyx::Types::TypeId> {
    size_t operator()(const Onyx::Types::TypeId& id) const noexcept {
        return std::hash<uint32_t>{}(id.value);
    }
};
