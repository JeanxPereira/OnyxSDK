#pragma once

#include <Onyx/Core/Result.h>

#include <cstddef>
#include <span>
#include <vector>

namespace Onyx::Vfs {

Result<size_t> Lz4Decompress(std::span<const std::byte> src, std::span<std::byte> dst);
constexpr size_t kLz4DefaultMaxOutput = size_t{256} << 20;

// Fails with code "lz4.tooLarge" once the decompressed output would exceed `maxOutput`.
Result<std::vector<std::byte>> Lz4FrameDecompress(std::span<const std::byte> src,
                                                  size_t maxOutput = kLz4DefaultMaxOutput);

}
