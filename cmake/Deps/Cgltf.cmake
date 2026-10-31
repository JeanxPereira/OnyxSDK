function(onyx_dep_cgltf)
    if(TARGET Onyx_cgltf)
        return()
    endif()
    include(FetchContent)
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
    FetchContent_Declare(cgltf
        GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
        GIT_TAG        v1.15
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  _skip_add_subdirectory)
    FetchContent_MakeAvailable(cgltf)
    add_library(Onyx_cgltf INTERFACE)
    add_library(cgltf::cgltf ALIAS Onyx_cgltf)
    target_include_directories(Onyx_cgltf INTERFACE "${cgltf_SOURCE_DIR}")
endfunction()
