#pragma once

#include <Onyx/Core/Domain/AssetEntry.h>
#include <Onyx/Core/Modules/Document.h>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace Onyx::Modules {

struct NodePath {
    std::vector<uint32_t> indices;
};

struct SelectionChanged {
    DocumentId doc;
    NodePath path;
};

// Null for an empty path or any index out of range at its depth.
const Domain::AssetEntry* Resolve(const Document& doc, const NodePath& path);

// First entry whose name equals `name` in depth-first, declaration order.
std::optional<NodePath> FindByName(const Document& doc, std::string_view name);

// Every entry whose name equals `name`, in depth-first, declaration order.
std::vector<NodePath> FindAllByName(const Document& doc, std::string_view name);

}
