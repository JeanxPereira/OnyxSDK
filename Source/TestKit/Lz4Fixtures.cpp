#include "Onyx/TestKit/Lz4Fixtures.h"

#include <lz4.h>
#include <lz4frame.h>

namespace Onyx::TestKit {

std::vector<std::byte> Lz4Compress(std::span<const std::byte> src) {
    std::vector<std::byte> out(static_cast<size_t>(LZ4_compressBound(static_cast<int>(src.size()))));
    const int n = LZ4_compress_default(reinterpret_cast<const char*>(src.data()),
                                       reinterpret_cast<char*>(out.data()),
                                       static_cast<int>(src.size()),
                                       static_cast<int>(out.size()));
    out.resize(n > 0 ? static_cast<size_t>(n) : 0);
    return out;
}

std::vector<std::byte> Lz4FrameCompress(std::span<const std::byte> src) {
    std::vector<std::byte> out(LZ4F_compressFrameBound(src.size(), nullptr));
    const size_t n = LZ4F_compressFrame(out.data(), out.size(), src.data(), src.size(), nullptr);
    out.resize(LZ4F_isError(n) ? 0 : n);
    return out;
}

}
