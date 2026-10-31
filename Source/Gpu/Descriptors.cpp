#include "Onyx/Gpu/Descriptors.h"

#include "Onyx/Gpu/Device.h"
#include "VkError.h"

namespace Onyx::Gpu {

Result<DescriptorPool> DescriptorPool::Create(Device& device, std::span<const VkDescriptorPoolSize> sizes,
                                              uint32_t maxSets) {
    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.maxSets = maxSets;
    info.poolSizeCount = static_cast<uint32_t>(sizes.size());
    info.pPoolSizes = sizes.data();
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkResult r = device.Fn().vkCreateDescriptorPool(device.Handle(), &info, nullptr, &pool);
    if (r != VK_SUCCESS) {
        return Result<DescriptorPool>::Fail(VkFail("gpu.vk", "vkCreateDescriptorPool", r));
    }
    return Result<DescriptorPool>::Ok(DescriptorPool(device, Unique<VkDescriptorPool>(&device, pool)));
}

Result<VkDescriptorSet> DescriptorPool::Allocate(VkDescriptorSetLayout layout) {
    VkDescriptorSetAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    info.descriptorPool = m_pool.Get();
    info.descriptorSetCount = 1;
    info.pSetLayouts = &layout;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkResult r = m_device->Fn().vkAllocateDescriptorSets(m_device->Handle(), &info, &set);
    if (r != VK_SUCCESS) {
        return Result<VkDescriptorSet>::Fail(VkFail("gpu.vk", "vkAllocateDescriptorSets", r));
    }
    return Result<VkDescriptorSet>::Ok(set);
}

void DescriptorPool::Reset() {
    m_device->Fn().vkResetDescriptorPool(m_device->Handle(), m_pool.Get(), 0);
}

Result<Unique<VkSampler>> CreateSampler(Device& device, const SamplerDesc& desc) {
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter = desc.filter;
    info.minFilter = desc.filter;
    info.mipmapMode = desc.filter == VK_FILTER_NEAREST ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.addressModeU = desc.address;
    info.addressModeV = desc.address;
    info.addressModeW = desc.address;
    info.maxLod = desc.mipmaps ? VK_LOD_CLAMP_NONE : 0.0f;
    const float limit = device.Info().maxAnisotropy;
    if (desc.maxAnisotropy > 0.0f && limit > 0.0f) {
        info.anisotropyEnable = VK_TRUE;
        info.maxAnisotropy = desc.maxAnisotropy < limit ? desc.maxAnisotropy : limit;
    }
    info.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    VkSampler sampler = VK_NULL_HANDLE;
    VkResult r = device.Fn().vkCreateSampler(device.Handle(), &info, nullptr, &sampler);
    if (r != VK_SUCCESS) {
        return Result<Unique<VkSampler>>::Fail(VkFail("gpu.vk", "vkCreateSampler", r));
    }
    return Result<Unique<VkSampler>>::Ok(Unique<VkSampler>(&device, sampler));
}

}
