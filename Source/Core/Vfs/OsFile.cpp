#include <Onyx/Core/Vfs/OsFile.h>

#include <algorithm>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace Onyx::Vfs {

namespace {

std::string Utf8(const std::filesystem::path& path) {
    const auto s = path.u8string();
    return std::string(s.begin(), s.end());
}

}

#ifdef _WIN32

Result<std::shared_ptr<OsFile>> OsFile::Open(const std::filesystem::path& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return Result<std::shared_ptr<OsFile>>::Fail(
            {"vfs.open", "cannot open " + Utf8(path), static_cast<int32_t>(GetLastError())});
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size)) {
        const auto err = static_cast<int32_t>(GetLastError());
        CloseHandle(h);
        return Result<std::shared_ptr<OsFile>>::Fail(
            {"vfs.open", "cannot stat " + Utf8(path), err});
    }
    std::shared_ptr<OsFile> file(new OsFile());
    file->m_handle = reinterpret_cast<intptr_t>(h);
    file->m_size = static_cast<uint64_t>(size.QuadPart);
    return Result<std::shared_ptr<OsFile>>::Ok(std::move(file));
}

OsFile::~OsFile() {
    if (m_handle != -1) CloseHandle(reinterpret_cast<HANDLE>(m_handle));
}

size_t OsFile::ReadAt(uint64_t offset, std::span<std::byte> dest) const {
    if (offset >= m_size) return 0;
    const uint64_t want = std::min<uint64_t>(dest.size(), m_size - offset);
    size_t total = 0;
    while (total < want) {
        const DWORD chunk = static_cast<DWORD>(std::min<uint64_t>(want - total, 1u << 30));
        OVERLAPPED ov{};
        const uint64_t at = offset + total;
        ov.Offset = static_cast<DWORD>(at & 0xFFFFFFFFu);
        ov.OffsetHigh = static_cast<DWORD>(at >> 32);
        DWORD got = 0;
        if (!ReadFile(reinterpret_cast<HANDLE>(m_handle), dest.data() + total, chunk, &got, &ov) ||
            got == 0) {
            break;
        }
        total += got;
    }
    return total;
}

#else

Result<std::shared_ptr<OsFile>> OsFile::Open(const std::filesystem::path& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return Result<std::shared_ptr<OsFile>>::Fail(
            {"vfs.open", "cannot open " + Utf8(path), errno});
    }
    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        const int err = errno;
        ::close(fd);
        return Result<std::shared_ptr<OsFile>>::Fail(
            {"vfs.open", "cannot stat " + Utf8(path), err});
    }
    std::shared_ptr<OsFile> file(new OsFile());
    file->m_handle = fd;
    file->m_size = static_cast<uint64_t>(st.st_size);
    return Result<std::shared_ptr<OsFile>>::Ok(std::move(file));
}

OsFile::~OsFile() {
    if (m_handle != -1) ::close(static_cast<int>(m_handle));
}

size_t OsFile::ReadAt(uint64_t offset, std::span<std::byte> dest) const {
    if (offset >= m_size) return 0;
    const uint64_t want = std::min<uint64_t>(dest.size(), m_size - offset);
    size_t total = 0;
    while (total < want) {
        const ssize_t got = ::pread(static_cast<int>(m_handle), dest.data() + total, want - total,
                                    static_cast<off_t>(offset + total));
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) break;
        total += static_cast<size_t>(got);
    }
    return total;
}

#endif

uint64_t OsFile::Size() const {
    return m_size;
}

}
