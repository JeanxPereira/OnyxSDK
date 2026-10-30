#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace Onyx::TestKit {

std::vector<std::byte> Lz4Compress(std::span<const std::byte> src);
std::vector<std::byte> Lz4FrameCompress(std::span<const std::byte> src, bool largeBlocks = false);

}
