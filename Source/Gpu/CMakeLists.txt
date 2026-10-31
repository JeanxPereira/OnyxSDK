include(${PROJECT_SOURCE_DIR}/cmake/Deps/VulkanHeaders.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/Deps/Volk.cmake)
include(${PROJECT_SOURCE_DIR}/cmake/Deps/Vma.cmake)
onyx_dep_vulkanheaders()
onyx_dep_volk()
onyx_dep_vma()

onyx_add_layer(Gpu
    PUBLIC_DEPS Onyx::Core volk::volk Vulkan::Headers GPUOpen::VulkanMemoryAllocator)
set_source_files_properties(VmaImpl.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
