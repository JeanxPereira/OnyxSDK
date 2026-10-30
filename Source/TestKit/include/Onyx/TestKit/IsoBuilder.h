#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Onyx::TestKit {

struct Corruption {
    enum Kind { RootSizePastEof, RecordLenBelow34, SelfPointingExtent };
};

class IsoBuilder {
public:
    IsoBuilder& AddFile(std::string_view path, std::span<const std::byte> bytes);
    IsoBuilder& AddDirectory(std::string_view path);

    std::vector<std::byte> Build();
    std::vector<std::byte> BuildCorrupt(Corruption::Kind kind);

private:
    struct Node {
        std::string name;
        int parent = -1;
        bool isDirectory = false;
        std::vector<std::byte> data;
        uint32_t extent = 0;
        uint32_t size = 0;
    };

    int FindOrAddDirectory(int parent, std::string_view name);
    std::vector<std::byte> Emit(uint32_t* rootOffset, uint32_t* firstSubdirRecordOffset);

    std::vector<Node> m_nodes{Node{"", -1, true, {}, 0, 0}};
};

}
