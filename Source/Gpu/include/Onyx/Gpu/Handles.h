#pragma once

#include <cstdint>
#include <utility>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "Onyx/Core/Result.h"

namespace Onyx::Gpu {

class Device;

// Owns one Vulkan handle. Destruction does not destroy immediately: the matching
// destroy call is queued on Device::Retire() at the last submitted fence value.
template <class T>
class Unique {
public:
    Unique() = default;
    Unique(Device* device, T handle) : m_device(device), m_handle(handle) {}
    Unique(const Unique&) = delete;
    Unique& operator=(const Unique&) = delete;
    Unique(Unique&& other) noexcept : m_device(other.m_device), m_handle(other.m_handle) {
        other.m_handle = VK_NULL_HANDLE;
    }
    Unique& operator=(Unique&& other) noexcept {
        if (this != &other) {
            Reset();
            m_device = other.m_device;
            m_handle = other.m_handle;
            other.m_handle = VK_NULL_HANDLE;
        }
        return *this;
    }
    ~Unique() { Reset(); }

    T Get() const { return m_handle; }
    Device* GetDevice() const { return m_device; }
    explicit operator bool() const { return m_handle != VK_NULL_HANDLE; }

    T Release() {
        T handle = m_handle;
        m_handle = VK_NULL_HANDLE;
        return handle;
    }

    void Reset();

private:
    Device* m_device = nullptr;
    T m_handle = VK_NULL_HANDLE;
};

enum class MemoryUsage {
    DeviceLocal,
    HostToDevice,
    DeviceToHost,
};

struct Buffer {
    Unique<VkBuffer> handle;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* mapped = nullptr;

    Buffer() = default;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;
    ~Buffer();

    void Reset();
};

struct ImageDesc {
    VkExtent2D extent{};
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageUsageFlags usage = 0;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    uint32_t mipLevels = 1;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
};

struct Image {
    Unique<VkImage> handle;
    VmaAllocation allocation = VK_NULL_HANDLE;
    Unique<VkImageView> view;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    uint32_t mipLevels = 1;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;

    Image() = default;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;
    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;
    ~Image();

    void Reset();
};

Result<Buffer> CreateBuffer(Device& device, VkDeviceSize size, VkBufferUsageFlags usage, MemoryUsage memory);
Result<Image> CreateImage(Device& device, const ImageDesc& desc);

}
