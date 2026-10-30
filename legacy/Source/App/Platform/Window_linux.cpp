#include <Onyx/App/Window.h>

#if defined(__linux__)

#include <GLFW/glfw3.h>

namespace Onyx::App {

// -- Platform-specific window hints -------------------------------------------
// GL context/profile hints are gone (T9: GLFW_CLIENT_API=GLFW_NO_API is set
// by initGLFW() itself, before this runs -- Vulkan needs no window-creation
// hint of its own beyond that). GLFW_DECORATED stays: the borderless
// styling this platform file's other methods handle is GL-agnostic, exactly
// as before.
void Window::configureGLFW() {
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
}

// -- Pre-window platform setup ------------------------------------------------
void Window::initNative() {
    // No-op on Linux for now
}

// -- Post-window platform setup -----------------------------------------------
void Window::setupNativeWindow() {
    // No-op on Linux for now
}

// -- Per-frame platform hooks -------------------------------------------------
void Window::beginNativeWindowFrame() {}
void Window::endNativeWindowFrame()   {}

} // namespace Onyx::App

#endif
