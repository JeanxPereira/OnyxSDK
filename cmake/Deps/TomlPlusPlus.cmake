function(onyx_dep_tomlplusplus)
    if(TARGET tomlplusplus::tomlplusplus)
        return()
    endif()
    include(FetchContent)
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
    FetchContent_Declare(tomlplusplus
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
        GIT_TAG        v3.4.0
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(tomlplusplus)
endfunction()
