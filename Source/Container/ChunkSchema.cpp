#include <Onyx/Container/ChunkSchema.h>

namespace Onyx::Container {

void ChunkSchema::SetContainer(std::string_view tag,
                               std::initializer_list<std::string_view> children) {
    auto& set = m_containers[std::string(tag)];
    for (std::string_view c : children) set.insert(std::string(c));
}

void ChunkSchema::SetLeaf(std::string_view tag) { m_leaves.insert(std::string(tag)); }

bool ChunkSchema::IsContainer(std::string_view tag) const {
    return m_containers.find(std::string(tag)) != m_containers.end();
}

bool ChunkSchema::Allows(std::string_view parent, std::string_view child) const {
    auto it = m_containers.find(std::string(parent));
    if (it == m_containers.end()) return false;
    return it->second.find(std::string(child)) != it->second.end();
}

} // namespace Onyx::Container
