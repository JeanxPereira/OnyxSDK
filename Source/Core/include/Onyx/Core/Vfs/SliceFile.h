#pragma once

#include <Onyx/Core/Vfs/IFile.h>

#include <memory>

namespace Onyx::Vfs {

class SliceFile final : public IFile {
public:
    SliceFile(std::shared_ptr<const IFile> parent, uint64_t base, uint64_t size);

    uint64_t Size() const override;
    size_t ReadAt(uint64_t offset, std::span<std::byte> dest) const override;

private:
    std::shared_ptr<const IFile> m_parent;
    uint64_t m_base = 0;
    uint64_t m_size = 0;
};

}
