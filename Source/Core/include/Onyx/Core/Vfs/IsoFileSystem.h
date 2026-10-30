#pragma once

#include <Onyx/Core/Result.h>
#include <Onyx/Core/Vfs/IFile.h>
#include <Onyx/Core/Vfs/IVirtualFileSystem.h>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Onyx::Vfs {

class IsoFileSystem final : public IVirtualFileSystem {
public:
    static Result<std::shared_ptr<IsoFileSystem>> Mount(std::shared_ptr<const IFile> image);

    std::vector<DirEntry> List(std::string_view dir) const override;
    std::shared_ptr<IFile> Open(std::string_view path) const override;
    bool Exists(std::string_view path) const override;

    std::vector<Error> Warnings() const { return m_warnings; }

private:
    struct Entry {
        DirEntry info;
        uint64_t offset = 0;
    };

    explicit IsoFileSystem(std::shared_ptr<const IFile> image);

    Result<void> Load();
    void WalkDirectory(uint32_t extent, uint32_t size, const std::string& path, int depth,
                       std::vector<uint32_t>& stack);
    void Warn(std::string message);

    std::shared_ptr<const IFile> m_image;
    std::unordered_map<std::string, Entry> m_entries;
    std::unordered_map<std::string, std::vector<DirEntry>> m_children;
    std::vector<Error> m_warnings;
};

}
