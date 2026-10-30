#pragma once

#include <Onyx/Core/Vfs/IFile.h>

#include <vector>

namespace Onyx::Vfs {

class MemoryFile final : public IFile {
public:
    explicit MemoryFile(std::vector<std::byte> bytes);

    uint64_t Size() const override;
    size_t ReadAt(uint64_t offset, std::span<std::byte> dest) const override;

private:
    std::vector<std::byte> m_bytes;
};

}
