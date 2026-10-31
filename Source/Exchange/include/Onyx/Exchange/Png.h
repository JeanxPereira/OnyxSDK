#pragma once

#include <Onyx/Core/Domain/Texture.h>
#include <Onyx/Core/Result.h>

#include <filesystem>

namespace Onyx::Exchange {

// Rgba8 only. Errors: exchange.png.format (any other format or an inconsistent texture), exchange.png.write.
Result<void> WritePng(const Domain::Texture& texture, const std::filesystem::path& out);

}
