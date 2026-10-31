function(onyx_dep_volk)
    if(TARGET volk)
        return()
    endif()
    include(${PROJECT_SOURCE_DIR}/cmake/Deps/VulkanHeaders.cmake)
    onyx_dep_vulkanheaders()
    include(FetchContent)
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
    FetchContent_Declare(volk
        GIT_REPOSITORY https://github.com/zeux/volk.git
        GIT_TAG        vulkan-sdk-1.3.296.0
        GIT_SHALLOW    TRUE)
    set(VOLK_PULL_IN_VULKAN OFF CACHE BOOL "" FORCE)
    set(VOLK_INSTALL        OFF CACHE BOOL "" FORCE)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(volk)
    target_link_libraries(volk PUBLIC Vulkan::Headers)
    target_compile_definitions(volk PUBLIC
        VK_NO_PROTOTYPES
        $<$<PLATFORM_ID:Windows>:VK_USE_PLATFORM_WIN32_KHR>)
    if(NOT WIN32)
        target_link_libraries(volk PUBLIC ${CMAKE_DL_LIBS})
    endif()
endfunction()
