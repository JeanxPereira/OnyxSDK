// vk_mem_alloc.h is header-only in the stb_image sense: exactly one
// translation unit in the whole link must define VMA_IMPLEMENTATION before
// including it, or every vma* call is an unresolved external. This is that
// TU, and nothing else lives here.
//
// volk first, always -- see VkContext.h's header comment for the rule this
// follows (VK_NO_PROTOTYPES must be defined, and vulkan/vulkan.h must not be
// the first Vulkan header a TU sees, or volk and the real prototypes fight
// over the same symbols).
#include <volk.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
