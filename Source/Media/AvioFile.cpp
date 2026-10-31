#include "AvioFile.h"

#include "Ffmpeg.h"

#include <algorithm>
#include <cstdio>
#include <span>

namespace Onyx::Media {

namespace {

constexpr int kBufferSize = 64 * 1024;

}

Result<std::unique_ptr<AvioFile>> AvioFile::Create(std::shared_ptr<const Vfs::IFile> file, uint64_t offset,
                                                    uint64_t size) {
    using Out = Result<std::unique_ptr<AvioFile>>;
    if (!file) return Out::Fail({"media.open", "no file to read the clip from"});
    const uint64_t total = file->Size();
    if (offset > total || size > total - offset) {
        return Out::Fail({"media.open", "the clip's byte range lies outside its file"});
    }
    std::unique_ptr<AvioFile> avio(new AvioFile());
    avio->m_file = std::move(file);
    avio->m_offset = offset;
    avio->m_size = size;

    auto* buffer = static_cast<unsigned char*>(av_malloc(kBufferSize));
    if (!buffer) return Out::Fail({"media.open", "out of memory for the read buffer"});
    avio->m_context = avio_alloc_context(buffer, kBufferSize, 0, avio.get(), &AvioFile::Read, nullptr, &AvioFile::Seek);
    if (!avio->m_context) {
        av_free(buffer);
        return Out::Fail({"media.open", "out of memory for the IO context"});
    }
    avio->m_context->seekable = AVIO_SEEKABLE_NORMAL;
    return Out::Ok(std::move(avio));
}

AvioFile::~AvioFile() {
    if (!m_context) return;
    av_freep(&m_context->buffer);
    avio_context_free(&m_context);
}

int AvioFile::Read(void* opaque, uint8_t* buffer, int size) {
    auto* self = static_cast<AvioFile*>(opaque);
    const uint64_t left = self->m_size - std::min(self->m_position, self->m_size);
    const size_t want = static_cast<size_t>(std::min<uint64_t>(left, static_cast<uint64_t>(std::max(size, 0))));
    if (want == 0) return AVERROR_EOF;
    const size_t got = self->m_file->ReadAt(self->m_offset + self->m_position,
                                            std::span<std::byte>(reinterpret_cast<std::byte*>(buffer), want));
    // The range promised these bytes: a file that ends early is a read error, not the end of the
    // clip.
    if (got == 0) return AVERROR(EIO);
    self->m_position += got;
    return static_cast<int>(got);
}

int64_t AvioFile::Seek(void* opaque, int64_t offset, int whence) {
    auto* self = static_cast<AvioFile*>(opaque);
    const auto size = static_cast<int64_t>(self->m_size);
    int64_t target = 0;
    switch (whence & ~AVSEEK_FORCE) {
    case AVSEEK_SIZE: return size;
    case SEEK_SET: target = offset; break;
    case SEEK_CUR: target = static_cast<int64_t>(self->m_position) + offset; break;
    case SEEK_END: target = size + offset; break;
    default: return AVERROR(EINVAL);
    }
    if (target < 0) return AVERROR(EINVAL);
    self->m_position = static_cast<uint64_t>(target);
    return target;
}

}
