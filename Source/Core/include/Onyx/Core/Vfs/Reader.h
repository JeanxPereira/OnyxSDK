#pragma once

#include <Onyx/Core/Vfs/IFile.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace Onyx::Vfs {

inline constexpr uint64_t npos = (std::numeric_limits<uint64_t>::max)();

class Reader {
public:
    explicit Reader(std::shared_ptr<const IFile> file, uint64_t base = 0, uint64_t size = npos);

    uint64_t Tell() const { return m_pos; }
    void Seek(uint64_t position) { m_pos = (std::min)(position, m_size); }
    uint64_t Size() const { return m_size; }
    uint64_t Remaining() const { return m_size - m_pos; }
    bool Eof() const { return m_pos >= m_size; }

    void SetBigEndian(bool bigEndian) { m_bigEndian = bigEndian; }
    bool BigEndian() const { return m_bigEndian; }

    size_t Read(std::span<std::byte> dest);

    template <class T>
    std::optional<T> Read() {
        static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>,
                      "Reader::Read<T> supports arithmetic and enum types only");
        std::array<std::byte, sizeof(T)> raw;
        const uint64_t start = m_pos;
        if (Read(std::span<std::byte>(raw)) != raw.size()) {
            m_pos = start;
            return std::nullopt;
        }
        if (m_bigEndian && (sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8)) {
            std::reverse(raw.begin(), raw.end());
        }
        T value;
        std::memcpy(&value, raw.data(), sizeof(T));
        return value;
    }

    std::optional<std::string> ReadString(size_t n);
    std::vector<std::byte> ReadAll();

private:
    std::shared_ptr<const IFile> m_file;
    uint64_t m_base = 0;
    uint64_t m_size = 0;
    uint64_t m_pos = 0;
    bool m_bigEndian = false;
};

}
