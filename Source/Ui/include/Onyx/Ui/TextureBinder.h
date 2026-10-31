#pragma once

#include <volk.h>

#include <imgui.h>

#include "Onyx/Core/Result.h"

namespace Onyx::Ui {

// Turns an image view into an id that ImGui draw commands can name, and releases that id again.
// The sampler argument is a hint that a binder may honour; a backend that owns its samplers (such as
// ImGuiVulkanBackend) ignores it and callers may pass VK_NULL_HANDLE.
class ITextureBinder {
public:
    virtual ~ITextureBinder() = default;

    virtual Result<ImTextureID> Bind(VkImageView view, VkImageLayout layout, VkSampler sampler) = 0;
    // The id stays valid for submissions that were already recorded; it must not appear in new ones.
    virtual void Unbind(ImTextureID id) = 0;
};

}
