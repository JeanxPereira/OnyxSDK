#pragma once

#include <Onyx/Core/Vfs/IFile.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Onyx::Vfs {

struct DirEntry {
    std::string name;
    bool isDirectory = false;
    uint64_t size = 0;
};

class IVirtualFileSystem {
public:
    virtual ~IVirtualFileSystem() = default;

    virtual std::vector<DirEntry> List(std::string_view dir) const = 0;
    virtual std::shared_ptr<IFile> Open(std::string_view path) const = 0;
    virtual bool Exists(std::string_view path) const = 0;
};

}
