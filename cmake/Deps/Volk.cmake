function(onyx_dep_volk)
    if(TARGET volk)
        return()
    endif()
    include(FetchContent)
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
    FetchContent_Declare(volk
        GIT_REPOSITORY https://github.com/zeux/volk.git
        GIT_TAG        vulkan-sdk-1.3.296.0
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(volk)
endfunction()
