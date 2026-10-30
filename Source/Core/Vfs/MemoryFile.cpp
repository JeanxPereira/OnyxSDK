#include <Onyx/Core/Vfs/MemoryFile.h>

#include <algorithm>
#include <cstring>

namespace Onyx::Vfs {

MemoryFile::MemoryFile(std::vector<std::byte> bytes) : m_bytes(std::move(bytes)) {}

uint64_t MemoryFile::Size() const {
    return m_bytes.size();
}

size_t MemoryFile::ReadAt(uint64_t offset, std::span<std::byte> dest) const {
    if (offset >= m_bytes.size()) return 0;
    const size_t n = static_cast<size_t>(std::min<uint64_t>(dest.size(), m_bytes.size() - offset));
    if (n == 0) return 0;
    std::memcpy(dest.data(), m_bytes.data() + offset, n);
    return n;
}

}
