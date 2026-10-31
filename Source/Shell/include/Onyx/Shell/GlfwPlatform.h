#pragma once

#include <memory>

#include "Onyx/Core/Result.h"
#include "Onyx/Shell/NativeWindow.h"

// Error codes: ui.backend.init  there is no current ImGui context or the GLFW backend refused the window.
namespace Onyx::Shell {

// The GLFW platform backend of ImGui bound to a window. It binds to the ImGui context that is current
// when Create runs and must be destroyed while that context is still current and before the window.
class GlfwPlatform {
public:
    static Result<std::unique_ptr<GlfwPlatform>> Create(NativeWindow& window);
    ~GlfwPlatform();

    GlfwPlatform(const GlfwPlatform&) = delete;
    GlfwPlatform& operator=(const GlfwPlatform&) = delete;

private:
    GlfwPlatform() = default;
};

}
