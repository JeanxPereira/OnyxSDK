# Sets ONYX_GLSLANG in the caller's scope to a glslang executable path, or to a
# generator expression naming the fetched glslang-standalone target. It is
# re-derived on every call and never stored in the cache as an expression.
function(onyx_dep_glslang)
    if(TARGET glslang-standalone)
        set(ONYX_GLSLANG "$<TARGET_FILE:glslang-standalone>" PARENT_SCOPE)
        return()
    endif()
    if(ONYX_GLSLANG AND NOT ONYX_GLSLANG MATCHES "^\\$<")
        set(ONYX_GLSLANG "${ONYX_GLSLANG}" PARENT_SCOPE)
        return()
    endif()
    unset(ONYX_GLSLANG CACHE)
    find_program(ONYX_GLSLANG NAMES glslang glslangValidator
        HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
    if(ONYX_GLSLANG)
        set(ONYX_GLSLANG "${ONYX_GLSLANG}" PARENT_SCOPE)
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
    set(ONYX_GLSLANG "$<TARGET_FILE:glslang-standalone>" PARENT_SCOPE)
endfunction()
