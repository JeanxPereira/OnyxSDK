function(onyx_dep_lz4)
    if(TARGET Onyx_lz4)
        return()
    endif()
    include(FetchContent)
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
    FetchContent_Declare(lz4
        GIT_REPOSITORY https://github.com/lz4/lz4.git
        GIT_TAG        v1.9.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(lz4)

    add_library(Onyx_lz4 STATIC
        "${lz4_SOURCE_DIR}/lib/lz4.c"
        "${lz4_SOURCE_DIR}/lib/lz4frame.c"
        "${lz4_SOURCE_DIR}/lib/lz4hc.c"
        "${lz4_SOURCE_DIR}/lib/xxhash.c")
    target_include_directories(Onyx_lz4 PUBLIC "${lz4_SOURCE_DIR}/lib")
endfunction()
