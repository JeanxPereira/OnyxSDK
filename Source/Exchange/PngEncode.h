#pragma once

#include <Onyx/Core/Domain/Texture.h>

#include <cstddef>
#include <vector>

namespace Onyx::Exchange {

// Encodes an Rgba8 texture; empty when the texture is not Rgba8, is inconsistent, or the encoder fails.
std::vector<std::byte> EncodePng(const Domain::Texture& texture);

}
