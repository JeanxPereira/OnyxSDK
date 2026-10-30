#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace Onyx::Vfs {

class IFile {
public:
    virtual ~IFile() = default;

    virtual uint64_t Size() const = 0;
    virtual size_t ReadAt(uint64_t offset, std::span<std::byte> dest) const = 0;
};

}
