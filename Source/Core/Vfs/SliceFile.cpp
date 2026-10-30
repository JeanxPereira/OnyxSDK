#include <Onyx/Core/Vfs/SliceFile.h>

#include <algorithm>

namespace Onyx::Vfs {

SliceFile::SliceFile(std::shared_ptr<const IFile> parent, uint64_t base, uint64_t size)
    : m_parent(std::move(parent)) {
    const uint64_t parentSize = m_parent ? m_parent->Size() : 0;
    m_base = std::min(base, parentSize);
    m_size = std::min(size, parentSize - m_base);
}

uint64_t SliceFile::Size() const {
    return m_size;
}

size_t SliceFile::ReadAt(uint64_t offset, std::span<std::byte> dest) const {
    if (!m_parent || offset >= m_size) return 0;
    const size_t n = static_cast<size_t>(std::min<uint64_t>(dest.size(), m_size - offset));
    return m_parent->ReadAt(m_base + offset, dest.first(n));
}

}
