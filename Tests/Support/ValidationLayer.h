#pragma once

#include <doctest/doctest.h>

#include <cstdlib>
#include <string_view>

#include "Onyx/Gpu/Device.h"

namespace Onyx {

// Validation checks hold vacuously on a machine without VK_LAYER_KHRONOS_validation, so a device that
// was asked for validation must have it. ONYX_NO_VALIDATION=1 opts out (for example on WSL, where the
// layers are not installed) and prints one message per executable.
inline void RequireValidationLayer(const Gpu::Device& device) {
    bool optOut = false;
#if defined(_WIN32)
    char* buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, "ONYX_NO_VALIDATION") == 0 && buf != nullptr) {
        optOut = std::string_view(buf) == "1";
        std::free(buf);
    }
#else
    const char* value = std::getenv("ONYX_NO_VALIDATION");
    optOut = value != nullptr && std::string_view(value) == "1";
#endif
    if (optOut) {
        static bool announced = false;
        if (!announced) {
            announced = true;
            MESSAGE("ONYX_NO_VALIDATION=1: validation message checks in this executable are vacuous");
        }
        return;
    }
    REQUIRE_MESSAGE(device.Info().validation,
                    "the validation layer is not available; install it or set ONYX_NO_VALIDATION=1");
}

}
