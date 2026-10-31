#include <Onyx/Core/Modules/Selection.h>

namespace Onyx::Modules {

namespace {

bool Search(const std::vector<Domain::AssetEntry>& level, std::string_view name, NodePath& path) {
    for (uint32_t i = 0; i < level.size(); ++i) {
        path.indices.push_back(i);
        if (level[i].name == name || Search(level[i].children, name, path)) return true;
        path.indices.pop_back();
    }
    return false;
}

void Collect(const std::vector<Domain::AssetEntry>& level, std::string_view name, NodePath& path,
             std::vector<NodePath>& found) {
    for (uint32_t i = 0; i < level.size(); ++i) {
        path.indices.push_back(i);
        if (level[i].name == name) found.push_back(path);
        Collect(level[i].children, name, path, found);
        path.indices.pop_back();
    }
}

}

const Domain::AssetEntry* Resolve(const Document& doc, const NodePath& path) {
    if (path.indices.empty() || path.indices[0] >= doc.roots.size()) return nullptr;
    const Domain::AssetEntry* node = &doc.roots[path.indices[0]];
    for (size_t i = 1; i < path.indices.size(); ++i) {
        if (path.indices[i] >= node->children.size()) return nullptr;
        node = &node->children[path.indices[i]];
    }
    return node;
}

std::optional<NodePath> FindByName(const Document& doc, std::string_view name) {
    NodePath path;
    if (Search(doc.roots, name, path)) return path;
    return std::nullopt;
}

std::vector<NodePath> FindAllByName(const Document& doc, std::string_view name) {
    std::vector<NodePath> found;
    NodePath path;
    Collect(doc.roots, name, path, found);
    return found;
}

}
