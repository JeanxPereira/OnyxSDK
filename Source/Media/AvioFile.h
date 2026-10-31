#pragma once

#include <Onyx/Core/Result.h>
#include <Onyx/Core/Vfs/IFile.h>

#include <cstdint>
#include <memory>

struct AVIOContext;

namespace Onyx::Media {

// A seekable, read-only AVIOContext over the window [offset, offset + size) of an IFile. FFmpeg sees
// the window as a whole file: position 0 is `offset`, and reads past `size` report end of file.
// Outlives the AVFormatContext that reads through it (the format context is opened with custom IO
// and never frees this one).
class AvioFile {
public:
    static Result<std::unique_ptr<AvioFile>> Create(std::shared_ptr<const Vfs::IFile> file, uint64_t offset,
                                                     uint64_t size);
    ~AvioFile();

    AvioFile(const AvioFile&) = delete;
    AvioFile& operator=(const AvioFile&) = delete;

    AVIOContext* Context() const { return m_context; }

private:
    AvioFile() = default;

    static int Read(void* opaque, uint8_t* buffer, int size);
    static int64_t Seek(void* opaque, int64_t offset, int whence);

    std::shared_ptr<const Vfs::IFile> m_file;
    uint64_t m_offset = 0;
    uint64_t m_size = 0;
    uint64_t m_position = 0;
    AVIOContext* m_context = nullptr;
};

}
