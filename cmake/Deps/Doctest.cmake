function(onyx_dep_doctest)
    if(TARGET doctest::doctest)
        return()
    endif()
    include(FetchContent)
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.4.11
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(doctest)
endfunction()
