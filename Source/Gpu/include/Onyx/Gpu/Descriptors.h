#pragma once

#include <cstdint>
#include <span>
#include <utility>

#include <volk.h>

#include "Onyx/Core/Result.h"
#include "Onyx/Gpu/Handles.h"

namespace Onyx::Gpu {

class Device;

class DescriptorPool {
public:
    static Result<DescriptorPool> Create(Device& device, std::span<const VkDescriptorPoolSize> sizes, uint32_t maxSets);

    DescriptorPool(DescriptorPool&&) noexcept = default;
    DescriptorPool& operator=(DescriptorPool&&) noexcept = default;
    DescriptorPool(const DescriptorPool&) = delete;
    DescriptorPool& operator=(const DescriptorPool&) = delete;

    Result<VkDescriptorSet> Allocate(VkDescriptorSetLayout layout);
    // Returns every set to the pool. The caller guarantees none is still in use by the GPU.
    void Reset();

    VkDescriptorPool Handle() const { return m_pool.Get(); }

private:
    DescriptorPool(Device& device, Unique<VkDescriptorPool> pool) : m_device(&device), m_pool(std::move(pool)) {}

    Device* m_device;
    Unique<VkDescriptorPool> m_pool;
};

struct SamplerDesc {
    VkFilter filter = VK_FILTER_LINEAR;
    VkSamplerAddressMode address = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    bool mipmaps = true;
    // Owned devices do not enable samplerAnisotropy, so this is not applied to them.
    float maxAnisotropy = 0.0f;
};

Result<Unique<VkSampler>> CreateSampler(Device& device, const SamplerDesc& desc);

}
