#pragma once
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace Onyx::Container {

// Maps a container tag to the set of child tags it may hold. Any tag not
// registered as a container is treated as a leaf by BuildChunkTree.
class ChunkSchema {
public:
    void SetContainer(std::string_view tag, std::initializer_list<std::string_view> children);
    void SetLeaf(std::string_view tag);

    bool IsContainer(std::string_view tag) const;
    bool Allows(std::string_view parent, std::string_view child) const;

private:
    std::unordered_map<std::string, std::unordered_set<std::string>> m_containers;
    std::unordered_set<std::string>                                  m_leaves;
};

} // namespace Onyx::Container
