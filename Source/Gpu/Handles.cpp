#include "Onyx/Gpu/Handles.h"

#include <cassert>

#include "Onyx/Gpu/Device.h"
#include "VkError.h"

// Non-dispatchable handles collapse to uint64_t on 32-bit targets, which makes the Unique<T> instantiations ambiguous.
static_assert(sizeof(void*) == 8, "Onyx::Gpu requires a 64-bit target");

namespace Onyx::Gpu {

namespace {

void Destroy(Device& d, VkBuffer h) { d.Fn().vkDestroyBuffer(d.Handle(), h, nullptr); }
void Destroy(Device& d, VkImage h) { d.Fn().vkDestroyImage(d.Handle(), h, nullptr); }
void Destroy(Device& d, VkImageView h) { d.Fn().vkDestroyImageView(d.Handle(), h, nullptr); }
void Destroy(Device& d, VkSampler h) { d.Fn().vkDestroySampler(d.Handle(), h, nullptr); }
void Destroy(Device& d, VkDescriptorPool h) { d.Fn().vkDestroyDescriptorPool(d.Handle(), h, nullptr); }
void Destroy(Device& d, VkDescriptorSetLayout h) { d.Fn().vkDestroyDescriptorSetLayout(d.Handle(), h, nullptr); }
void Destroy(Device& d, VkPipelineLayout h) { d.Fn().vkDestroyPipelineLayout(d.Handle(), h, nullptr); }
void Destroy(Device& d, VkPipeline h) { d.Fn().vkDestroyPipeline(d.Handle(), h, nullptr); }
void Destroy(Device& d, VkCommandPool h) { d.Fn().vkDestroyCommandPool(d.Handle(), h, nullptr); }
void Destroy(Device& d, VkSemaphore h) { d.Fn().vkDestroySemaphore(d.Handle(), h, nullptr); }
void Destroy(Device& d, VkShaderModule h) { d.Fn().vkDestroyShaderModule(d.Handle(), h, nullptr); }

}

template <class T>
void Unique<T>::Reset() {
    if (m_handle == VK_NULL_HANDLE) {
        return;
    }
    assert(m_device != nullptr && "Unique holds a handle but no Device");
    Device* device = m_device;
    T handle = m_handle;
    m_handle = VK_NULL_HANDLE;
    device->Retire().Enqueue([device, handle] { Destroy(*device, handle); }, device->GraphicsQueue().LastSubmitted());
}

template class Unique<VkBuffer>;
template class Unique<VkImage>;
template class Unique<VkImageView>;
template class Unique<VkSampler>;
template class Unique<VkDescriptorPool>;
template class Unique<VkDescriptorSetLayout>;
template class Unique<VkPipelineLayout>;
template class Unique<VkPipeline>;
template class Unique<VkCommandPool>;
template class Unique<VkSemaphore>;
template class Unique<VkShaderModule>;

Buffer::Buffer(Buffer&& other) noexcept
    : handle(std::move(other.handle)), allocation(other.allocation), size(other.size), mapped(other.mapped) {
    other.allocation = VK_NULL_HANDLE;
    other.mapped = nullptr;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        Reset();
        handle = std::move(other.handle);
        allocation = other.allocation;
        size = other.size;
        mapped = other.mapped;
        other.allocation = VK_NULL_HANDLE;
        other.mapped = nullptr;
    }
    return *this;
}

Buffer::~Buffer() {
    Reset();
}

void Buffer::Reset() {
    if (handle) {
        Device* device = handle.GetDevice();
        assert(device != nullptr);
        VkBuffer buffer = handle.Release();
        VmaAllocation owned = allocation;
        device->Retire().Enqueue([device, buffer, owned] { vmaDestroyBuffer(device->Allocator(), buffer, owned); },
                                 device->GraphicsQueue().LastSubmitted());
    }
    allocation = VK_NULL_HANDLE;
    mapped = nullptr;
    size = 0;
}

Image::Image(Image&& other) noexcept
    : handle(std::move(other.handle)),
      allocation(other.allocation),
      view(std::move(other.view)),
      format(other.format),
      extent(other.extent),
      mipLevels(other.mipLevels),
      samples(other.samples) {
    other.allocation = VK_NULL_HANDLE;
}

Image& Image::operator=(Image&& other) noexcept {
    if (this != &other) {
        Reset();
        handle = std::move(other.handle);
        allocation = other.allocation;
        view = std::move(other.view);
        format = other.format;
        extent = other.extent;
        mipLevels = other.mipLevels;
        samples = other.samples;
        other.allocation = VK_NULL_HANDLE;
    }
    return *this;
}

Image::~Image() {
    Reset();
}

void Image::Reset() {
    view.Reset();
    if (handle) {
        Device* device = handle.GetDevice();
        assert(device != nullptr);
        VkImage image = handle.Release();
        VmaAllocation owned = allocation;
        device->Retire().Enqueue([device, image, owned] { vmaDestroyImage(device->Allocator(), image, owned); },
                                 device->GraphicsQueue().LastSubmitted());
    }
    allocation = VK_NULL_HANDLE;
}

Result<Buffer> CreateBuffer(Device& device, VkDeviceSize size, VkBufferUsageFlags usage, MemoryUsage memory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    switch (memory) {
    case MemoryUsage::DeviceLocal:
        break;
    case MemoryUsage::HostToDevice:
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    case MemoryUsage::DeviceToHost:
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    }

    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VmaAllocationInfo info{};
    VkResult r = vmaCreateBuffer(device.Allocator(), &bufferInfo, &allocInfo, &buffer, &allocation, &info);
    if (r != VK_SUCCESS) {
        return Result<Buffer>::Fail(VkFail("gpu.alloc", "vmaCreateBuffer", r));
    }

    Buffer out;
    out.handle = Unique<VkBuffer>(&device, buffer);
    out.allocation = allocation;
    out.size = size;
    out.mapped = info.pMappedData;
    return Result<Buffer>::Ok(std::move(out));
}

Result<Image> CreateImage(Device& device, const ImageDesc& desc) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = desc.format;
    imageInfo.extent = {desc.extent.width, desc.extent.height, 1};
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = desc.samples;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = desc.usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkResult r = vmaCreateImage(device.Allocator(), &imageInfo, &allocInfo, &image, &allocation, nullptr);
    if (r != VK_SUCCESS) {
        return Result<Image>::Fail(VkFail("gpu.alloc", "vmaCreateImage", r));
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = desc.format;
    viewInfo.subresourceRange = {desc.aspect, 0, desc.mipLevels, 0, 1};
    VkImageView view = VK_NULL_HANDLE;
    r = device.Fn().vkCreateImageView(device.Handle(), &viewInfo, nullptr, &view);
    if (r != VK_SUCCESS) {
        vmaDestroyImage(device.Allocator(), image, allocation);
        return Result<Image>::Fail(VkFail("gpu.alloc", "vkCreateImageView", r));
    }

    Image out;
    out.handle = Unique<VkImage>(&device, image);
    out.allocation = allocation;
    out.view = Unique<VkImageView>(&device, view);
    out.format = desc.format;
    out.extent = desc.extent;
    out.mipLevels = desc.mipLevels;
    out.samples = desc.samples;
    return Result<Image>::Ok(std::move(out));
}

}
