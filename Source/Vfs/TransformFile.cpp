#include <Onyx/Vfs/TransformFile.h>

#include <cstdint>
#include <utility>

namespace Onyx::Vfs {

TransformFile::TransformFile(std::shared_ptr<IFile> inner, ByteTransform fn)
    : m_inner(std::move(inner)), m_fn(std::move(fn)) {}

size_t TransformFile::Read(void* dest, size_t bytes) {
    const int64_t pos = m_inner->Tell();   // absolute offset of the first byte
    const size_t  n   = m_inner->Read(dest, bytes);
    auto* p = static_cast<uint8_t*>(dest);
    for (size_t i = 0; i < n; ++i)
        p[i] = m_fn(p[i], static_cast<uint64_t>(pos) + i);
    return n;
}

bool    TransformFile::Seek(int64_t offset, int origin) { return m_inner->Seek(offset, origin); }
int64_t TransformFile::Tell()        { return m_inner->Tell(); }
size_t  TransformFile::Size()  const { return m_inner->Size(); }
bool    TransformFile::IsEOF() const { return m_inner->IsEOF(); }
bool    TransformFile::IsValid() const { return m_inner->IsValid(); }

std::shared_ptr<IFile> MakeXorFile(std::shared_ptr<IFile> inner, uint8_t key) {
    if (key == 0) return inner;
    return std::make_shared<TransformFile>(
        std::move(inner),
        [key](uint8_t b, uint64_t) { return static_cast<uint8_t>(b ^ key); });
}

} // namespace Onyx::Vfs
