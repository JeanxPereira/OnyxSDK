#include "Onyx/TestKit/IsoBuilder.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace Onyx::TestKit {

namespace {

constexpr uint32_t kSector = 2048;
constexpr uint32_t kFirstDirSector = 20;

using Bytes = std::vector<std::byte>;

void Put8(Bytes& b, size_t at, uint32_t v) { b[at] = static_cast<std::byte>(v & 0xFF); }

void PutLe16(Bytes& b, size_t at, uint32_t v) {
    Put8(b, at, v);
    Put8(b, at + 1, v >> 8);
}

void PutBe16(Bytes& b, size_t at, uint32_t v) {
    Put8(b, at, v >> 8);
    Put8(b, at + 1, v);
}

void PutLe32(Bytes& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) Put8(b, at + i, v >> (8 * i));
}

void PutBe32(Bytes& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) Put8(b, at + i, v >> (8 * (3 - i)));
}

void PutBoth32(Bytes& b, size_t at, uint32_t v) {
    PutLe32(b, at, v);
    PutBe32(b, at + 4, v);
}

void PutBoth16(Bytes& b, size_t at, uint32_t v) {
    PutLe16(b, at, v);
    PutBe16(b, at + 2, v);
}

uint32_t SectorsFor(uint64_t bytes) {
    return static_cast<uint32_t>((bytes + kSector - 1) / kSector);
}

Bytes MakeRecord(uint32_t extent, uint32_t size, bool directory, std::string_view name) {
    size_t len = 33 + name.size();
    if (len % 2) ++len;
    Bytes r(len, std::byte{0});
    Put8(r, 0, static_cast<uint32_t>(len));
    PutBoth32(r, 2, extent);
    PutBoth32(r, 10, size);
    r[25] = directory ? std::byte{2} : std::byte{0};
    PutBoth16(r, 28, 1);
    Put8(r, 32, static_cast<uint32_t>(name.size()));
    std::memcpy(r.data() + 33, name.data(), name.size());
    return r;
}

std::string IsoName(const std::string& name, bool directory) {
    std::string up;
    for (char c : name) up += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (directory) return up.substr(0, 31);
    std::string stem = up;
    std::string ext;
    if (const size_t dot = up.rfind('.'); dot != std::string::npos) {
        stem = up.substr(0, dot);
        ext = up.substr(dot);
    }
    const size_t room = 31 - 2 - std::min<size_t>(ext.size(), 28);
    return stem.substr(0, room) + ext + ";1";
}

std::vector<std::string> SplitPath(std::string_view path) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find_first_of("/\\", start);
        if (end == std::string_view::npos) end = path.size();
        if (end > start) parts.emplace_back(path.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

}

int IsoBuilder::FindOrAddDirectory(int parent, std::string_view name) {
    for (size_t i = 1; i < m_nodes.size(); ++i) {
        if (m_nodes[i].parent == parent && m_nodes[i].isDirectory && m_nodes[i].name == name) {
            return static_cast<int>(i);
        }
    }
    Node node;
    node.name = std::string(name);
    node.parent = parent;
    node.isDirectory = true;
    m_nodes.push_back(std::move(node));
    return static_cast<int>(m_nodes.size() - 1);
}

IsoBuilder& IsoBuilder::AddDirectory(std::string_view path) {
    int current = 0;
    for (const auto& part : SplitPath(path)) current = FindOrAddDirectory(current, part);
    return *this;
}

IsoBuilder& IsoBuilder::AddFile(std::string_view path, std::span<const std::byte> bytes) {
    auto parts = SplitPath(path);
    if (parts.empty()) return *this;
    int current = 0;
    for (size_t i = 0; i + 1 < parts.size(); ++i) current = FindOrAddDirectory(current, parts[i]);
    Node node;
    node.name = parts.back();
    node.parent = current;
    node.data.assign(bytes.begin(), bytes.end());
    node.size = static_cast<uint32_t>(bytes.size());
    m_nodes.push_back(std::move(node));
    return *this;
}

Bytes IsoBuilder::Emit(uint32_t* rootOffset, uint32_t* firstSubdirRecordOffset) {
    const size_t count = m_nodes.size();

    std::vector<std::vector<int>> children(count);
    for (size_t i = 1; i < count; ++i) children[m_nodes[i].parent].push_back(static_cast<int>(i));
    for (auto& list : children) {
        std::sort(list.begin(), list.end(), [&](int a, int b) {
            return IsoName(m_nodes[a].name, m_nodes[a].isDirectory) <
                   IsoName(m_nodes[b].name, m_nodes[b].isDirectory);
        });
    }

    std::vector<int> order{0};
    for (size_t head = 0; head < order.size(); ++head) {
        for (int child : children[order[head]]) {
            if (m_nodes[child].isDirectory) order.push_back(child);
        }
    }

    const std::string selfName(1, '\0');
    const std::string parentName(1, '\1');

    auto layoutDirectory = [&](int dir, const std::vector<uint32_t>& extents,
                               std::vector<uint32_t>* subdirOffsets) {
        Bytes out;
        auto append = [&](const Bytes& rec) {
            const size_t used = out.size() % kSector;
            if (used + rec.size() > kSector) out.resize(out.size() + (kSector - used), std::byte{0});
            out.insert(out.end(), rec.begin(), rec.end());
        };
        const int parent = m_nodes[dir].parent < 0 ? dir : m_nodes[dir].parent;
        append(MakeRecord(extents[dir], 0, true, selfName));
        append(MakeRecord(extents[parent], 0, true, parentName));
        for (int child : children[dir]) {
            const Node& n = m_nodes[child];
            const Bytes rec = MakeRecord(extents[child], n.size, n.isDirectory,
                                         IsoName(n.name, n.isDirectory));
            if (subdirOffsets && n.isDirectory && subdirOffsets->empty()) {
                const size_t used = out.size() % kSector;
                const size_t at = used + rec.size() > kSector ? out.size() + (kSector - used) : out.size();
                subdirOffsets->push_back(static_cast<uint32_t>(at));
            }
            append(rec);
        }
        out.resize(SectorsFor(out.size()) * kSector, std::byte{0});
        return out;
    };

    std::vector<uint32_t> extents(count, 0);
    std::vector<uint32_t> dirSectors(count, 0);
    uint32_t next = kFirstDirSector;
    for (int dir : order) {
        extents[dir] = next;
        dirSectors[dir] = SectorsFor(layoutDirectory(dir, extents, nullptr).size());
        next += dirSectors[dir];
    }
    for (size_t i = 1; i < count; ++i) {
        if (m_nodes[i].isDirectory) continue;
        extents[i] = next;
        next += std::max<uint32_t>(1, SectorsFor(m_nodes[i].size));
    }
    const uint32_t totalSectors = next;

    std::vector<uint32_t> subdirOffsets;
    Bytes image(static_cast<size_t>(totalSectors) * kSector, std::byte{0});
    for (int dir : order) {
        const Bytes content = layoutDirectory(dir, extents, dir == 0 ? &subdirOffsets : nullptr);
        std::memcpy(image.data() + static_cast<size_t>(extents[dir]) * kSector, content.data(), content.size());
    }
    for (size_t i = 1; i < count; ++i) {
        if (m_nodes[i].isDirectory || m_nodes[i].data.empty()) continue;
        std::memcpy(image.data() + static_cast<size_t>(extents[i]) * kSector,
                    m_nodes[i].data.data(), m_nodes[i].data.size());
    }

    std::vector<int> orderIndex(count, 0);
    for (size_t i = 0; i < order.size(); ++i) orderIndex[order[i]] = static_cast<int>(i) + 1;
    Bytes pathL;
    Bytes pathM;
    for (int dir : order) {
        const std::string name = dir == 0 ? selfName : IsoName(m_nodes[dir].name, true);
        const uint32_t parentNumber = dir == 0 ? 1 : static_cast<uint32_t>(orderIndex[m_nodes[dir].parent]);
        size_t len = 8 + name.size();
        if (len % 2) ++len;
        Bytes recL(len, std::byte{0});
        Bytes recM(len, std::byte{0});
        Put8(recL, 0, static_cast<uint32_t>(name.size()));
        Put8(recM, 0, static_cast<uint32_t>(name.size()));
        PutLe32(recL, 2, extents[dir]);
        PutBe32(recM, 2, extents[dir]);
        PutLe16(recL, 6, parentNumber);
        PutBe16(recM, 6, parentNumber);
        std::memcpy(recL.data() + 8, name.data(), name.size());
        std::memcpy(recM.data() + 8, name.data(), name.size());
        pathL.insert(pathL.end(), recL.begin(), recL.end());
        pathM.insert(pathM.end(), recM.begin(), recM.end());
    }
    std::memcpy(image.data() + 18 * kSector, pathL.data(), std::min<size_t>(pathL.size(), kSector));
    std::memcpy(image.data() + 19 * kSector, pathM.data(), std::min<size_t>(pathM.size(), kSector));

    Bytes pvd(kSector, std::byte{0});
    pvd[0] = std::byte{1};
    std::memcpy(pvd.data() + 1, "CD001", 5);
    pvd[6] = std::byte{1};
    std::fill(pvd.begin() + 8, pvd.begin() + 72, std::byte{' '});
    std::fill(pvd.begin() + 190, pvd.begin() + 813, std::byte{' '});
    PutBoth32(pvd, 80, totalSectors);
    PutBoth16(pvd, 120, 1);
    PutBoth16(pvd, 124, 1);
    PutBoth16(pvd, 128, kSector);
    PutBoth32(pvd, 132, static_cast<uint32_t>(pathL.size()));
    PutLe32(pvd, 140, 18);
    PutBe32(pvd, 148, 19);
    const Bytes root = MakeRecord(extents[0], dirSectors[0] * kSector, true, selfName);
    std::memcpy(pvd.data() + 156, root.data(), 34);
    pvd[881] = std::byte{1};
    std::memcpy(image.data() + 16 * kSector, pvd.data(), kSector);

    Bytes terminator(kSector, std::byte{0});
    terminator[0] = std::byte{255};
    std::memcpy(terminator.data() + 1, "CD001", 5);
    terminator[6] = std::byte{1};
    std::memcpy(image.data() + 17 * kSector, terminator.data(), kSector);

    if (rootOffset) *rootOffset = extents[0] * kSector;
    if (firstSubdirRecordOffset) {
        *firstSubdirRecordOffset = subdirOffsets.empty() ? 0 : extents[0] * kSector + subdirOffsets.front();
    }
    return image;
}

Bytes IsoBuilder::Build() {
    return Emit(nullptr, nullptr);
}

Bytes IsoBuilder::BuildCorrupt(Corruption::Kind kind) {
    if (kind == Corruption::SelfPointingExtent) {
        bool hasSubdir = false;
        for (size_t i = 1; i < m_nodes.size(); ++i) {
            if (m_nodes[i].isDirectory && m_nodes[i].parent == 0) hasSubdir = true;
        }
        if (!hasSubdir) AddDirectory("LOOP");
    }

    uint32_t rootOffset = 0;
    uint32_t subdirOffset = 0;
    Bytes image = Emit(&rootOffset, &subdirOffset);
    const uint32_t lastSector = static_cast<uint32_t>(image.size() / kSector) - 1;

    switch (kind) {
    case Corruption::RootSizePastEof: {
        const size_t rec = 16 * kSector + 156;
        PutBoth32(image, rec + 2, lastSector);
        PutBoth32(image, rec + 10, 0xFFFFFFFFu);
        break;
    }
    case Corruption::RecordLenBelow34:
        Put8(image, rootOffset, 20);
        break;
    case Corruption::SelfPointingExtent:
        PutBoth32(image, subdirOffset + 2, rootOffset / kSector);
        break;
    }
    return image;
}

}
