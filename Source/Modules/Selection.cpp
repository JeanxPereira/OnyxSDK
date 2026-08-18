#include <Onyx/Modules/Selection.h>

namespace Onyx::Modules {

const Domain::AssetEntry* Resolve(const Document& doc, const NodePath& path) {
    if (path.indices.empty()) return nullptr;

    const uint32_t rootIdx = path.indices[0];
    if (rootIdx >= doc.roots.size()) return nullptr;
    const Domain::AssetEntry* node = &doc.roots[rootIdx];

    for (size_t i = 1; i < path.indices.size(); ++i) {
        const uint32_t childIdx = path.indices[i];
        if (childIdx >= node->children.size()) return nullptr;
        node = &node->children[childIdx];
    }
    return node;
}

} // namespace Onyx::Modules
