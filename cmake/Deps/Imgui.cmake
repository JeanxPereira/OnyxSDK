# Defines imgui::imgui (core + Vulkan backend) and, once glfw exists, imgui::glfw.
# A host that already provides imgui::imgui keeps its own copy.
function(onyx_dep_imgui)
    include(FetchContent)
    if(NOT TARGET imgui::imgui)
        include(${PROJECT_SOURCE_DIR}/cmake/Deps/VulkanHeaders.cmake)
        include(${PROJECT_SOURCE_DIR}/cmake/Deps/Volk.cmake)
        onyx_dep_vulkanheaders()
        onyx_dep_volk()
        set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
        FetchContent_Declare(imgui
            GIT_REPOSITORY https://github.com/ocornut/imgui.git
            GIT_TAG        2af6dd9694288e6befe1edb7ce25510911693c22
            SOURCE_SUBDIR  _skip_add_subdirectory)
        FetchContent_MakeAvailable(imgui)
        FetchContent_GetProperties(imgui SOURCE_DIR imgui_dir)
        add_library(Onyx_imgui STATIC
            ${imgui_dir}/imgui.cpp
            ${imgui_dir}/imgui_draw.cpp
            ${imgui_dir}/imgui_tables.cpp
            ${imgui_dir}/imgui_widgets.cpp
            ${imgui_dir}/imgui_demo.cpp
            ${imgui_dir}/backends/imgui_impl_vulkan.cpp)
        add_library(imgui::imgui ALIAS Onyx_imgui)
        target_include_directories(Onyx_imgui PUBLIC ${imgui_dir} ${imgui_dir}/backends)
        target_compile_definitions(Onyx_imgui PUBLIC
            IMGUI_IMPL_VULKAN_USE_VOLK
            IMGUI_DEFINE_MATH_OPERATORS
            IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
        target_link_libraries(Onyx_imgui PUBLIC volk::volk Vulkan::Headers)
        set_target_properties(Onyx_imgui PROPERTIES CXX_SCAN_FOR_MODULES OFF)
    else()
        message(STATUS "Onyx: using the host's imgui::imgui")
    endif()

    if(TARGET glfw AND NOT TARGET imgui::glfw)
        FetchContent_GetProperties(imgui SOURCE_DIR imgui_dir)
        if(imgui_dir)
            add_library(Onyx_imgui_glfw STATIC ${imgui_dir}/backends/imgui_impl_glfw.cpp)
            add_library(imgui::glfw ALIAS Onyx_imgui_glfw)
            target_link_libraries(Onyx_imgui_glfw PUBLIC imgui::imgui glfw)
            set_target_properties(Onyx_imgui_glfw PROPERTIES CXX_SCAN_FOR_MODULES OFF)
        endif()
    endif()
endfunction()
