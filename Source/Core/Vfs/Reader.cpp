#include <Onyx/Core/Vfs/Reader.h>

namespace Onyx::Vfs {

Reader::Reader(std::shared_ptr<const IFile> file, uint64_t base, uint64_t size)
    : m_file(std::move(file)) {
    const uint64_t fileSize = m_file ? m_file->Size() : 0;
    m_base = std::min(base, fileSize);
    m_size = std::min(size, fileSize - m_base);
}

size_t Reader::Read(std::span<std::byte> dest) {
    if (!m_file || Eof()) return 0;
    const size_t want = static_cast<size_t>(std::min<uint64_t>(dest.size(), Remaining()));
    const size_t got = m_file->ReadAt(m_base + m_pos, dest.first(want));
    m_pos += got;
    return got;
}

std::optional<std::string> Reader::ReadString(size_t n) {
    if (n > Remaining()) return std::nullopt;
    std::string out(n, '\0');
    const uint64_t start = m_pos;
    if (Read(std::as_writable_bytes(std::span(out))) != n) {
        m_pos = start;
        return std::nullopt;
    }
    return out;
}

std::vector<std::byte> Reader::ReadAll() {
    std::vector<std::byte> out(static_cast<size_t>(Remaining()));
    out.resize(Read(out));
    return out;
}

}
