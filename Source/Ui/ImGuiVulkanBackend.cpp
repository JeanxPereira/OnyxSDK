#include "Onyx/Ui/ImGuiVulkanBackend.h"

#include <algorithm>
#include <climits>
#include <format>
#include <string>

#include "imgui_impl_vulkan.h"

namespace Onyx::Ui {

namespace {

constexpr uint32_t kDescriptorPoolSize = 1024;

// ImGui reports Vulkan failures through a plain function pointer, so the failing result of the call
// in progress is parked here for the caller to read straight after.
thread_local VkResult t_lastResult = VK_SUCCESS;

// Keeps the first failure until it is read.
void RecordResult(VkResult result) {
    if (result != VK_SUCCESS && t_lastResult == VK_SUCCESS) {
        t_lastResult = result;
    }
}

VkResult TakeResult() {
    VkResult result = t_lastResult;
    t_lastResult = VK_SUCCESS;
    return result;
}

std::string ResultName(VkResult result) {
    switch (result) {
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    default: return std::format("VkResult {}", static_cast<int>(result));
    }
}

PFN_vkVoidFunction LoadByName(const char* name, void* user) {
    Gpu::Device& device = *static_cast<Gpu::Device*>(user);
    PFN_vkVoidFunction fn = vkGetDeviceProcAddr(device.Handle(), name);
    return fn != nullptr ? fn : vkGetInstanceProcAddr(device.Instance(), name);
}

}

Result<std::unique_ptr<ImGuiVulkanBackend>> ImGuiVulkanBackend::Create(Gpu::Device& device,
                                                                      const BackendOptions& options) {
    using Out = Result<std::unique_ptr<ImGuiVulkanBackend>>;
    if (ImGui::GetCurrentContext() == nullptr) {
        return Out::Fail({"ui.backend.init", "there is no current ImGui context", 0});
    }
    if (ImGui::GetIO().BackendRendererUserData != nullptr) {
        return Out::Fail({"ui.backend.init", "the current ImGui context already has a renderer backend", 0});
    }

    if (!ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3, &LoadByName, &device)) {
        return Out::Fail({"ui.backend.init", "ImGui could not resolve the Vulkan entry points", 0});
    }

    const VkFormat colorFormat = options.colorFormat;
    const uint32_t imageCount = (std::max)(options.imageCount, 2u);

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = device.Instance();
    info.PhysicalDevice = device.PhysicalDevice();
    info.Device = device.Handle();
    info.QueueFamily = device.GraphicsQueue().Family();
    info.Queue = device.GraphicsQueue().Handle();
    info.DescriptorPoolSize = kDescriptorPoolSize;
    info.MinImageCount = imageCount;
    info.ImageCount = imageCount;
    info.PipelineCache = device.PipelineCache();
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.MSAASamples = options.samples;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;
    info.CheckVkResultFn = &RecordResult;

    TakeResult();
    if (!ImGui_ImplVulkan_Init(&info)) {
        return Out::Fail({"ui.backend.init", "ImGui_ImplVulkan_Init failed", 0});
    }
    const VkResult initResult = TakeResult();
    if (initResult != VK_SUCCESS) {
        ImGui_ImplVulkan_Shutdown();
        return Out::Fail({"ui.backend.init",
                          "ImGui_ImplVulkan_Init reported " + ResultName(initResult),
                          static_cast<int32_t>(initResult)});
    }

    return Out::Ok(std::unique_ptr<ImGuiVulkanBackend>(new ImGuiVulkanBackend(device)));
}

ImGuiVulkanBackend::~ImGuiVulkanBackend() {
    Gpu::Queue& queue = m_device.GraphicsQueue();
    if (!queue.Wait(queue.LastSubmitted()).ok()) {
        (void)queue.WaitIdle();
    }
    // Descriptor sets queued by Unbind are freed through ImGui, which is about to go away.
    m_device.Retire().Collect(queue.Completed());
    ImGui_ImplVulkan_Shutdown();
}

Result<void> ImGuiVulkanBackend::Fault() const {
    const VkResult result = TakeResult();
    if (result == VK_SUCCESS) {
        return Result<void>::Ok();
    }
    return Result<void>::Fail({"ui.backend.init",
                               "imgui reported " + ResultName(result),
                               static_cast<int32_t>(result)});
}

void ImGuiVulkanBackend::NewFrame() {
    ImGui_ImplVulkan_NewFrame();
}

void ImGuiVulkanBackend::Record(VkCommandBuffer cmd, ImDrawData* drawData) {
    if (drawData == nullptr) {
        return;
    }
    ImGui_ImplVulkan_RenderDrawData(drawData, cmd);
}

void ImGuiVulkanBackend::RebuildFonts() {
    Gpu::Queue& queue = m_device.GraphicsQueue();
    if (!queue.Wait(queue.LastSubmitted()).ok()) {
        (void)queue.WaitIdle();
    }
    for (ImTextureData* texture : ImGui::GetPlatformIO().Textures) {
        if (texture->Status != ImTextureStatus_OK) {
            continue;
        }
        texture->SetStatus(ImTextureStatus_WantDestroy);
        texture->UnusedFrames = INT_MAX;
        ImGui_ImplVulkan_UpdateTexture(texture);
        texture->SetStatus(ImTextureStatus_WantCreate);
    }
}

Result<ImTextureID> ImGuiVulkanBackend::Bind(VkImageView view, VkImageLayout layout, VkSampler) {
    const VkResult parked = TakeResult();
    VkDescriptorSet set = ImGui_ImplVulkan_AddTexture(view, layout);
    const VkResult result = TakeResult();
    t_lastResult = parked;
    if (result != VK_SUCCESS || set == VK_NULL_HANDLE) {
        return Result<ImTextureID>::Fail(
            {"ui.backend.init", "ImGui_ImplVulkan_AddTexture failed with " + ResultName(result),
             static_cast<int32_t>(result)});
    }
    return Result<ImTextureID>::Ok(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(set)));
}

void ImGuiVulkanBackend::Unbind(ImTextureID id) {
    if (id == ImTextureID_Invalid) {
        return;
    }
    VkDescriptorSet set = reinterpret_cast<VkDescriptorSet>(static_cast<uintptr_t>(id));
    m_device.Retire().Enqueue([set] { ImGui_ImplVulkan_RemoveTexture(set); },
                              m_device.GraphicsQueue().LastSubmitted());
}

}
