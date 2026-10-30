#pragma once

#include <Onyx/Core/Result.h>

#include <cstddef>
#include <span>
#include <vector>

namespace Onyx::Vfs {

Result<size_t> Lz4Decompress(std::span<const std::byte> src, std::span<std::byte> dst);
Result<std::vector<std::byte>> Lz4FrameDecompress(std::span<const std::byte> src);

}
