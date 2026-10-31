#include "Onyx/Gpu/Swapchain.h"

#include <algorithm>
#include <array>

#include "Onyx/Gpu/Device.h"
#include "Enumerate.h"
#include "VkError.h"

namespace Onyx::Gpu {

namespace {

VkSurfaceFormatKHR ChooseFormat(const std::vector<VkSurfaceFormatKHR>& formats, VkFormat preferred) {
    for (const auto& f : formats) {
        if (f.format == preferred && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    for (const auto& f : formats) {
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    return formats.front();
}

VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes, bool vsync) {
    if (!vsync) {
        for (VkPresentModeKHR wanted : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR}) {
            if (std::find(modes.begin(), modes.end(), wanted) != modes.end()) {
                return wanted;
            }
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkCompositeAlphaFlagBitsKHR ChooseCompositeAlpha(VkCompositeAlphaFlagsKHR supported) {
    for (VkCompositeAlphaFlagBitsKHR candidate :
         {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
          VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
        if ((supported & candidate) != 0) {
            return candidate;
        }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

}

Result<std::unique_ptr<Swapchain>> Swapchain::Create(Device& device, const SwapchainOptions& options) {
    using Out = Result<std::unique_ptr<Swapchain>>;
    if (device.Fn().vkCreateSwapchainKHR == nullptr) {
        return Out::Fail({"gpu.present", "VK_KHR_swapchain is not enabled on this device", 0});
    }
    if (options.surface == VK_NULL_HANDLE) {
        return Out::Fail({"gpu.surface", "no surface was given", 0});
    }
    VkBool32 supported = VK_FALSE;
    VkResult r = device.InstanceFn().getPhysicalDeviceSurfaceSupportKHR(
        device.PhysicalDevice(), device.GraphicsQueue().Family(), options.surface, &supported);
    if (r != VK_SUCCESS) {
        return Out::Fail(VkFail("gpu.vk", "vkGetPhysicalDeviceSurfaceSupportKHR", r));
    }
    if (supported != VK_TRUE) {
        return Out::Fail({"gpu.surface", "the graphics queue family cannot present to this surface", 0});
    }

    std::unique_ptr<Swapchain> swapchain(new Swapchain(device, options));
    auto built = swapchain->Build(options.extent);
    if (!built.ok()) {
        return Out::Fail(built.error());
    }
    return Out::Ok(std::move(swapchain));
}

Swapchain::~Swapchain() {
    if (m_swapchain == VK_NULL_HANDLE) {
        return;
    }
    // A failed wait is already recorded in Queue::Fault(); the swapchain is destroyed regardless.
    m_device.GraphicsQueue().WaitIdle();
    DestroyViews();
    m_device.Fn().vkDestroySwapchainKHR(m_device.Handle(), m_swapchain, nullptr);
}

void Swapchain::DestroyViews() {
    for (VkImageView view : m_views) {
        m_device.Fn().vkDestroyImageView(m_device.Handle(), view, nullptr);
    }
    m_views.clear();
    m_images.clear();
}

Result<void> Swapchain::Build(VkExtent2D requested) {
    const auto& fn = m_device.Fn();
    const auto& ifn = m_device.InstanceFn();
    VkPhysicalDevice physical = m_device.PhysicalDevice();
    VkSurfaceKHR surface = m_options.surface;

    VkSurfaceCapabilitiesKHR caps{};
    VkResult r = ifn.getPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);
    if (r != VK_SUCCESS) {
        return Result<void>::Fail(VkFail("gpu.vk", "vkGetPhysicalDeviceSurfaceCapabilitiesKHR", r));
    }

    auto formatList = Enumerate<VkSurfaceFormatKHR>("vkGetPhysicalDeviceSurfaceFormatsKHR", [&](uint32_t* n, VkSurfaceFormatKHR* p) {
        return ifn.getPhysicalDeviceSurfaceFormatsKHR(physical, surface, n, p);
    });
    if (!formatList.ok()) {
        return Result<void>::Fail(formatList.error());
    }
    std::vector<VkSurfaceFormatKHR> formats = std::move(formatList.value());
    if (formats.empty()) {
        return Result<void>::Fail({"gpu.vk", "the surface reports no formats", 0});
    }

    auto modeList = Enumerate<VkPresentModeKHR>("vkGetPhysicalDeviceSurfacePresentModesKHR", [&](uint32_t* n, VkPresentModeKHR* p) {
        return ifn.getPhysicalDeviceSurfacePresentModesKHR(physical, surface, n, p);
    });
    if (!modeList.ok()) {
        return Result<void>::Fail(modeList.error());
    }
    std::vector<VkPresentModeKHR> modes = std::move(modeList.value());

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width = std::clamp(requested.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent.height = std::clamp(requested.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0) {
        return Result<void>::Fail({"gpu.extent", "the surface has no drawable area", 0});
    }

    const VkSurfaceFormatKHR format = ChooseFormat(formats, m_options.preferredFormat);
    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) {
        imageCount = std::min(imageCount, caps.maxImageCount);
    }

    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = surface;
    info.minImageCount = imageCount;
    info.imageFormat = format.format;
    info.imageColorSpace = format.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0) {
        info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = ChooseCompositeAlpha(caps.supportedCompositeAlpha);
    info.presentMode = ChoosePresentMode(modes, m_options.vsync);
    info.clipped = VK_TRUE;
    info.oldSwapchain = m_swapchain;

    VkSwapchainKHR created = VK_NULL_HANDLE;
    r = fn.vkCreateSwapchainKHR(m_device.Handle(), &info, nullptr, &created);
    if (r != VK_SUCCESS) {
        return Result<void>::Fail(VkFail("gpu.vk", "vkCreateSwapchainKHR", r));
    }

    uint32_t actualCount = 0;
    r = fn.vkGetSwapchainImagesKHR(m_device.Handle(), created, &actualCount, nullptr);
    std::vector<VkImage> images(actualCount);
    if (r == VK_SUCCESS) {
        r = fn.vkGetSwapchainImagesKHR(m_device.Handle(), created, &actualCount, images.data());
    }
    std::vector<VkImageView> views;
    if (r == VK_SUCCESS) {
        for (VkImage image : images) {
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = format.format;
            viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkImageView view = VK_NULL_HANDLE;
            r = fn.vkCreateImageView(m_device.Handle(), &viewInfo, nullptr, &view);
            if (r != VK_SUCCESS) {
                break;
            }
            views.push_back(view);
        }
    }
    if (r != VK_SUCCESS) {
        for (VkImageView view : views) {
            fn.vkDestroyImageView(m_device.Handle(), view, nullptr);
        }
        fn.vkDestroySwapchainKHR(m_device.Handle(), created, nullptr);
        return Result<void>::Fail(VkFail("gpu.vk", "swapchain images and views", r));
    }

    // The caller guarantees the old swapchain is idle (Recreate waits for the device).
    DestroyViews();
    if (m_swapchain != VK_NULL_HANDLE) {
        fn.vkDestroySwapchainKHR(m_device.Handle(), m_swapchain, nullptr);
    }
    m_swapchain = created;
    m_images = std::move(images);
    m_views = std::move(views);
    m_format = format.format;
    m_extent = extent;
    m_needsRecreate = false;
    return Result<void>::Ok();
}

Result<std::optional<SwapImage>> Swapchain::Acquire(VkSemaphore signal) {
    using Out = Result<std::optional<SwapImage>>;
    uint32_t index = 0;
    VkResult r = m_device.Fn().vkAcquireNextImageKHR(m_device.Handle(), m_swapchain, UINT64_MAX, signal,
                                                     VK_NULL_HANDLE, &index);
    switch (r) {
    case VK_SUCCESS:
        break;
    case VK_SUBOPTIMAL_KHR:
        m_needsRecreate = true;
        break;
    case VK_ERROR_OUT_OF_DATE_KHR:
        m_needsRecreate = true;
        return Out::Ok(std::nullopt);
    default:
        return Out::Fail(VkFail("gpu.vk", "vkAcquireNextImageKHR", r));
    }
    return Out::Ok(SwapImage{m_images[index], m_views[index], index});
}

Result<void> Swapchain::Present(std::span<const VkSemaphore> wait, uint32_t index) {
    VkPresentInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = static_cast<uint32_t>(wait.size());
    info.pWaitSemaphores = wait.data();
    info.swapchainCount = 1;
    info.pSwapchains = &m_swapchain;
    info.pImageIndices = &index;

    auto presented = m_device.GraphicsQueue().Present(info);
    if (!presented.ok()) {
        return Result<void>::Fail(presented.error());
    }
    const VkResult r = presented.value();
    switch (r) {
    case VK_SUCCESS:
        return Result<void>::Ok();
    case VK_SUBOPTIMAL_KHR:
    case VK_ERROR_OUT_OF_DATE_KHR:
        m_needsRecreate = true;
        return Result<void>::Ok();
    default:
        return Result<void>::Fail(VkFail("gpu.vk", "vkQueuePresentKHR", r));
    }
}

Result<void> Swapchain::Recreate(VkExtent2D extent) {
    if (extent.width == 0 || extent.height == 0) {
        return Result<void>::Fail({"gpu.extent", "swapchain extent must be non-zero", 0});
    }
    auto idle = m_device.GraphicsQueue().WaitIdle();
    if (!idle.ok()) {
        return idle;
    }
    return Build(extent);
}

}
