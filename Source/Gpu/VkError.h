#pragma once

#include <cstdint>
#include <format>

#include <volk.h>

#include "Onyx/Core/Result.h"

namespace Onyx::Gpu {

// Layer-private: builds an Error that carries the raw VkResult in `native`.
inline Error VkFail(const char* code, const char* what, VkResult result) {
    return {code, std::format("{} failed (VkResult {})", what, static_cast<int>(result)), static_cast<int32_t>(result)};
}

}
