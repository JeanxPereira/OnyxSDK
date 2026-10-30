#pragma once

#include <Onyx/Core/Domain/ByteRange.h>
#include <Onyx/Core/Domain/MediaKind.h>
#include <Onyx/Core/Types/TypeId.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Onyx::Domain {

enum class NodeFlags : uint8_t {
    None = 0,
    Failed = 1,
};

constexpr NodeFlags operator|(NodeFlags a, NodeFlags b) {
    return static_cast<NodeFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

constexpr NodeFlags operator&(NodeFlags a, NodeFlags b) {
    return static_cast<NodeFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}

constexpr bool HasFlag(NodeFlags value, NodeFlags flag) {
    return (value & flag) == flag;
}

struct AssetEntry {
    std::string name;
    std::string subtitle;
    ByteRange source;
    Types::TypeId type;
    MediaKind kind = MediaKind::Unknown;
    NodeFlags flags = NodeFlags::None;
    std::vector<AssetEntry> children;
};

}
