function(onyx_dep_glslang)
    if(ONYX_GLSLANG)
        return()
    endif()
    find_program(ONYX_GLSLANG NAMES glslang glslangValidator
        HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
    if(ONYX_GLSLANG)
        return()
    endif()
    include(FetchContent)
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
    FetchContent_Declare(glslang
        GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
        GIT_TAG        14.3.0
        GIT_SHALLOW    TRUE)
    set(ENABLE_OPT              OFF CACHE BOOL "" FORCE)
    set(GLSLANG_TESTS           OFF CACHE BOOL "" FORCE)
    set(ENABLE_GLSLANG_BINARIES ON  CACHE BOOL "" FORCE)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(glslang)
    set(ONYX_GLSLANG "$<TARGET_FILE:glslang-standalone>" CACHE INTERNAL "glslang compiler")
endfunction()
