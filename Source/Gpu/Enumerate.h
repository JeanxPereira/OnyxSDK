#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include <volk.h>

#include "Onyx/Core/Result.h"
#include "VkError.h"

namespace Onyx::Gpu {

// Layer-private: runs a two-call enumeration, repeating while the driver reports VK_INCOMPLETE.
template <class T, class Fn>
Result<std::vector<T>> Enumerate(const char* what, Fn&& call) {
    std::vector<T> items;
    uint32_t count = 0;
    VkResult r = VK_SUCCESS;
    do {
        r = call(&count, nullptr);
        if (r != VK_SUCCESS) {
            break;
        }
        items.resize(count);
        r = count == 0 ? VK_SUCCESS : call(&count, items.data());
    } while (r == VK_INCOMPLETE);
    if (r != VK_SUCCESS) {
        return Result<std::vector<T>>::Fail(VkFail("gpu.vk", what, r));
    }
    items.resize(count);
    return Result<std::vector<T>>::Ok(std::move(items));
}

}
