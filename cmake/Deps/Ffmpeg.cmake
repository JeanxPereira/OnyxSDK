function(onyx_dep_ffmpeg)
    if(TARGET ffmpeg::ffmpeg)
        return()
    endif()
    add_library(onyx_ffmpeg INTERFACE)
    add_library(ffmpeg::ffmpeg ALIAS onyx_ffmpeg)

    if(WIN32)
        include(FetchContent)
        set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
        set(ONYX_FFMPEG_WIN_URL
            "https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-03-31-13-11/ffmpeg-n7.1.3-43-g5a1f107b4c-win64-lgpl-shared-7.1.zip"
            CACHE STRING "FFmpeg 7.x win64 LGPL shared archive used by Onyx (not the GPL build)")
        FetchContent_Declare(ffmpeg_prebuilt
            URL "${ONYX_FFMPEG_WIN_URL}"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(ffmpeg_prebuilt)

        file(GLOB ffmpeg_subdirs "${ffmpeg_prebuilt_SOURCE_DIR}/ffmpeg-*")
        if(ffmpeg_subdirs)
            list(GET ffmpeg_subdirs 0 ffmpeg_root)
        else()
            set(ffmpeg_root "${ffmpeg_prebuilt_SOURCE_DIR}")
        endif()
        if(NOT EXISTS "${ffmpeg_root}/include/libavformat/avformat.h")
            message(FATAL_ERROR
                "FFmpeg headers not found at ${ffmpeg_root}/include. The archive layout changed "
                "or the extraction failed. Delete the ffmpeg_prebuilt directories under _deps "
                "and reconfigure, or configure with -DONYX_WITH_MEDIA=OFF.")
        endif()

        target_include_directories(onyx_ffmpeg INTERFACE "${ffmpeg_root}/include")
        foreach(lib avformat avcodec avutil swscale swresample)
            target_link_libraries(onyx_ffmpeg INTERFACE "${ffmpeg_root}/lib/${lib}.lib")
        endforeach()
        file(GLOB ffmpeg_dlls "${ffmpeg_root}/bin/*.dll")
        set(ONYX_FFMPEG_DLLS "${ffmpeg_dlls}" CACHE INTERNAL "FFmpeg runtime DLLs for Onyx executables")
    else()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(FFMPEG IMPORTED_TARGET
                libavformat libavcodec libavutil libswscale libswresample)
        endif()
        if(NOT TARGET PkgConfig::FFMPEG)
            message(FATAL_ERROR
                "Onyx::Media needs FFmpeg 6 or newer with pkg-config. Install "
                "libavformat-dev libavcodec-dev libavutil-dev libswscale-dev "
                "libswresample-dev pkg-config, or configure with -DONYX_WITH_MEDIA=OFF.")
        endif()
        target_link_libraries(onyx_ffmpeg INTERFACE PkgConfig::FFMPEG)
    endif()
endfunction()
