function(onyx_dep_vma)
    if(TARGET GPUOpen::VulkanMemoryAllocator)
        return()
    endif()
    include(${PROJECT_SOURCE_DIR}/cmake/Deps/VulkanHeaders.cmake)
    onyx_dep_vulkanheaders()
    include(FetchContent)
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
    FetchContent_Declare(vma
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG        v3.1.0
        GIT_SHALLOW    TRUE)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(vma)
    target_link_libraries(VulkanMemoryAllocator INTERFACE Vulkan::Headers)
endfunction()
