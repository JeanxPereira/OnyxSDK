#pragma once

#include <Onyx/Core/Result.h>
#include <Onyx/Core/Vfs/IFile.h>

#include <filesystem>
#include <memory>

namespace Onyx::Vfs {

class OsFile final : public IFile {
public:
    static Result<std::shared_ptr<OsFile>> Open(const std::filesystem::path& path);

    ~OsFile() override;
    OsFile(const OsFile&) = delete;
    OsFile& operator=(const OsFile&) = delete;

    uint64_t Size() const override;
    size_t ReadAt(uint64_t offset, std::span<std::byte> dest) const override;

private:
    OsFile() = default;

    intptr_t m_handle = -1;
    uint64_t m_size = 0;
};

}
