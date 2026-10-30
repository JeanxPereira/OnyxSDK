#include <Onyx/Core/Vfs/Lz4.h>

#include <lz4.h>
#include <lz4frame.h>

#include <limits>

namespace Onyx::Vfs {

Result<size_t> Lz4Decompress(std::span<const std::byte> src, std::span<std::byte> dst) {
    if (src.size() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        dst.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return Result<size_t>::Fail({"lz4.size", "Buffer exceeds the LZ4 block size limit"});
    }
    const int written = LZ4_decompress_safe(reinterpret_cast<const char*>(src.data()),
                                            reinterpret_cast<char*>(dst.data()),
                                            static_cast<int>(src.size()),
                                            static_cast<int>(dst.size()));
    if (written < 0) {
        return Result<size_t>::Fail({"lz4.block", "Corrupt or truncated LZ4 block", written});
    }
    return Result<size_t>::Ok(static_cast<size_t>(written));
}

Result<std::vector<std::byte>> Lz4FrameDecompress(std::span<const std::byte> src) {
    using Out = Result<std::vector<std::byte>>;
    LZ4F_dctx* ctx = nullptr;
    const size_t created = LZ4F_createDecompressionContext(&ctx, LZ4F_VERSION);
    if (LZ4F_isError(created)) {
        return Out::Fail({"lz4.frame", "Cannot create LZ4 frame context", static_cast<int32_t>(created)});
    }

    std::vector<std::byte> out;
    std::vector<std::byte> chunk(64 * 1024);
    const std::byte* in = src.data();
    size_t remaining = src.size();
    size_t hint = 1;
    for (;;) {
        size_t dstSize = chunk.size();
        size_t srcSize = remaining;
        hint = LZ4F_decompress(ctx, chunk.data(), &dstSize, in, &srcSize, nullptr);
        if (LZ4F_isError(hint)) {
            const int32_t native = static_cast<int32_t>(hint);
            LZ4F_freeDecompressionContext(ctx);
            return Out::Fail({"lz4.frame", "Corrupt LZ4 frame", native});
        }
        out.insert(out.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(dstSize));
        in += srcSize;
        remaining -= srcSize;
        if (hint == 0) break;
        if (srcSize == 0 && dstSize == 0) break;
    }
    LZ4F_freeDecompressionContext(ctx);
    if (hint != 0) {
        return Out::Fail({"lz4.frame", "Truncated LZ4 frame"});
    }
    return Out::Ok(std::move(out));
}

}
