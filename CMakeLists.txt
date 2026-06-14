cmake_minimum_required(VERSION 3.20)
project(OnyxSDK LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(ONYX_BUILD_EXAMPLES "Build the Onyx example apps" ON)
option(ONYX_BUILD_TESTS    "Build the Onyx engine unit tests" ON)

include(FetchContent)

# ── GLFW ───────────────────────────────────────────────────────────────────
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.3.9
    GIT_SHALLOW    TRUE
    UPDATE_DISCONNECTED TRUE
)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# ── GLM (header-only math) ────────────────────────────────────────────────
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
    UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(glm)

# ── lz4 ───────────────────────────────────────────────────────────────────
FetchContent_Declare(
    lz4
    GIT_REPOSITORY https://github.com/lz4/lz4.git
    GIT_TAG        v1.9.4
    GIT_SHALLOW    TRUE
    UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(lz4)

add_library(lz4_lib STATIC
    ${lz4_SOURCE_DIR}/lib/lz4.c
    ${lz4_SOURCE_DIR}/lib/lz4frame.c
    ${lz4_SOURCE_DIR}/lib/lz4hc.c
    ${lz4_SOURCE_DIR}/lib/xxhash.c
)
target_include_directories(lz4_lib PUBLIC ${lz4_SOURCE_DIR}/lib)

# ── glad (manual, vendored) ────────────────────────────────────────────────
add_library(glad STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/src/glad.c
)
target_include_directories(glad PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/include
)

# ── miniaudio (single-header audio, vendored) ────────────────────────────
add_library(miniaudio STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio/miniaudio.c
)
target_include_directories(miniaudio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/miniaudio)

# ── ImGui lib (vendored copy in third_party/imgui) ────────────────────────
add_library(imgui_lib STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/imgui_demo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_glfw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends
)
target_link_libraries(imgui_lib PUBLIC glfw glad)

# ── ImPlot (submodule at third_party/implot) ─────────────────────────────
add_library(implot_lib STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/implot/implot.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/implot/implot_items.cpp
)
target_include_directories(implot_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/implot)
target_link_libraries(implot_lib PUBLIC imgui_lib)

# ── ImGuiColorTextEdit (submodule at third_party/imgui_color_text_edit) ──
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui_color_text_edit/TextEditor.cpp")
    message(FATAL_ERROR
        "third_party/imgui_color_text_edit is empty. "
        "Run: git submodule update --init --recursive")
endif()
add_library(imgui_color_text_edit STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui_color_text_edit/TextEditor.cpp
)
target_include_directories(imgui_color_text_edit PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui_color_text_edit
)
target_link_libraries(imgui_color_text_edit PUBLIC imgui_lib)
target_compile_features(imgui_color_text_edit PUBLIC cxx_std_17)

# ── FFmpeg ────────────────────────────────────────────────────────────────
add_library(ffmpeg_lib INTERFACE)

if(WIN32)
    FetchContent_Declare(
        ffmpeg_prebuilt
        URL      "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n7.1-latest-win64-gpl-shared-7.1.zip"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(ffmpeg_prebuilt)

    file(GLOB FFMPEG_SUBDIRS "${ffmpeg_prebuilt_SOURCE_DIR}/ffmpeg-*")
    if(FFMPEG_SUBDIRS)
        list(GET FFMPEG_SUBDIRS 0 FFMPEG_ROOT)
    else()
        set(FFMPEG_ROOT "${ffmpeg_prebuilt_SOURCE_DIR}")
    endif()

    if(NOT EXISTS "${FFMPEG_ROOT}/include/libavformat/avformat.h")
        message(FATAL_ERROR
            "FFmpeg headers not found at ${FFMPEG_ROOT}/include. "
            "FetchContent extraction likely failed or upstream zip layout "
            "changed. Delete build/_deps/ffmpeg_prebuilt-* and reconfigure.")
    endif()

    target_include_directories(ffmpeg_lib INTERFACE "${FFMPEG_ROOT}/include")
    target_link_libraries(ffmpeg_lib INTERFACE
        "${FFMPEG_ROOT}/lib/avformat.lib"
        "${FFMPEG_ROOT}/lib/avcodec.lib"
        "${FFMPEG_ROOT}/lib/avutil.lib"
        "${FFMPEG_ROOT}/lib/swscale.lib"
        "${FFMPEG_ROOT}/lib/swresample.lib"
    )

    file(GLOB FFMPEG_DLLS "${FFMPEG_ROOT}/bin/*.dll")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED
        libavformat
        libavcodec
        libswscale
        libswresample
        libavutil
    )

    target_include_directories(ffmpeg_lib INTERFACE ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(ffmpeg_lib      INTERFACE ${FFMPEG_LIBRARIES})
    target_compile_options(ffmpeg_lib     INTERFACE ${FFMPEG_CFLAGS_OTHER})
    target_link_directories(ffmpeg_lib    INTERFACE ${FFMPEG_LIBRARY_DIRS})
endif()

# Export FFMPEG_DLLS so consumers (FetchContent) can copy the DLLs
if(WIN32 AND FFMPEG_DLLS)
    set(ONYX_FFMPEG_DLLS "${FFMPEG_DLLS}" CACHE INTERNAL "FFmpeg runtime DLLs for Onyx consumers")
endif()

# ── The Onyx engine library ────────────────────────────────────────────────
file(GLOB_RECURSE ONYX_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
)
if(APPLE)
    file(GLOB_RECURSE ONYX_OBJC CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.mm"
        "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.m"
    )
    list(APPEND ONYX_SOURCES ${ONYX_OBJC})
endif()

add_library(Onyx STATIC ${ONYX_SOURCES})
add_library(Onyx::Onyx ALIAS Onyx)

target_include_directories(Onyx
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/Include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

target_compile_features(Onyx PUBLIC cxx_std_20)

target_link_libraries(Onyx PUBLIC
    imgui_lib
    implot_lib
    imgui_color_text_edit
    glfw
    glad
    glm::glm
    lz4_lib
    miniaudio
    ffmpeg_lib
)

if(MSVC)
    target_compile_options(Onyx PUBLIC /utf-8)
endif()

target_compile_definitions(Onyx PUBLIC
    $<$<PLATFORM_ID:Windows>:GLFW_EXPOSE_NATIVE_WIN32>
    $<$<PLATFORM_ID:Darwin>:GLFW_EXPOSE_NATIVE_COCOA>
    $<$<PLATFORM_ID:Windows>:GOWTOOL_OS_WINDOWS>
    $<$<PLATFORM_ID:Darwin>:GOWTOOL_OS_MACOS>
    $<$<PLATFORM_ID:Linux>:GOWTOOL_OS_LINUX>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
)

if(WIN32)
    target_link_libraries(Onyx PUBLIC opengl32 comdlg32 dwmapi)
elseif(APPLE)
    target_link_libraries(Onyx PUBLIC
        "-framework OpenGL"
        "-framework AudioToolbox"
        "-framework CoreAudio"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreFoundation"
    )
else()
    target_link_libraries(Onyx PUBLIC GL dl pthread m)
endif()

# Provide the engine's runtime font dir to consumers
set(ONYX_FONTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts" CACHE INTERNAL "Onyx bundled fonts")

if(ONYX_BUILD_EXAMPLES)
    add_subdirectory(Examples/MinimalViewer)
endif()

if(ONYX_BUILD_TESTS)
    enable_testing()

    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.4.11
        GIT_SHALLOW    TRUE
        UPDATE_DISCONNECTED TRUE
    )
    set(DOCTEST_WITH_TESTS    OFF CACHE BOOL "" FORCE)
    set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF CACHE BOOL "" FORCE)
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
    FetchContent_MakeAvailable(doctest)
    unset(CMAKE_POLICY_VERSION_MINIMUM CACHE)

    add_subdirectory(Tests)
endif()
