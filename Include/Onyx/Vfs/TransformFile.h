#pragma once
#include <Onyx/Vfs/IFile.h>
#include <cstdint>
#include <functional>
#include <memory>

namespace Onyx::Vfs {

// IFile decorator: forwards to `inner`, applying `fn(byte, absoluteOffset)` to
// every byte returned by Read. Seek/Tell/Size/IsEOF/IsValid pass through.
class TransformFile : public IFile {
public:
    using ByteTransform = std::function<uint8_t(uint8_t /*b*/, uint64_t /*absOffset*/)>;
    TransformFile(std::shared_ptr<IFile> inner, ByteTransform fn);

    size_t  Read(void* dest, size_t bytes) override;
    bool    Seek(int64_t offset, int origin) override;
    int64_t Tell() override;
    size_t  Size() const override;
    bool    IsEOF() const override;
    bool    IsValid() const override;

private:
    std::shared_ptr<IFile> m_inner;
    ByteTransform          m_fn;
};

// Convenience: single-byte XOR cipher. key == 0 returns `inner` unchanged.
std::shared_ptr<IFile> MakeXorFile(std::shared_ptr<IFile> inner, uint8_t key);

} // namespace Onyx::Vfs
