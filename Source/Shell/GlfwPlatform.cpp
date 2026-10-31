#include "Onyx/Shell/GlfwPlatform.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>

namespace Onyx::Shell {

Result<std::unique_ptr<GlfwPlatform>> GlfwPlatform::Create(NativeWindow& window) {
    using Out = Result<std::unique_ptr<GlfwPlatform>>;
    if (ImGui::GetCurrentContext() == nullptr) {
        return Out::Fail({"ui.backend.init", "there is no current ImGui context", 0});
    }
    if (!ImGui_ImplGlfw_InitForVulkan(window.Handle(), true)) {
        return Out::Fail({"ui.backend.init", "ImGui_ImplGlfw_InitForVulkan failed", 0});
    }
    return Out::Ok(std::unique_ptr<GlfwPlatform>(new GlfwPlatform()));
}

GlfwPlatform::~GlfwPlatform() { ImGui_ImplGlfw_Shutdown(); }

}
