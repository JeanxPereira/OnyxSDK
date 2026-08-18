cmake_minimum_required(VERSION 3.20)
project(OnyxSDK VERSION 0.6.0 LANGUAGES CXX C)

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

# ── Dear ImGui (docking) via FetchContent ──────────────────────────────────
# Pinned to the exact commit previously vendored under third_party/imgui
# (1.92.9 WIP, build 19282). SOURCE_SUBDIR points at a non-existent dir so
# MakeAvailable only populates the source — ImGui ships no CMakeLists and we
# build the static lib ourselves below.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        2af6dd9694288e6befe1edb7ce25510911693c22
    SOURCE_SUBDIR  _skip_add_subdirectory
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC glfw glad)

# ── ImPlot via FetchContent (pinned to the previously-submoduled commit) ───
FetchContent_Declare(implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG        d65a2bef53d32502407de3a4be80f191e2f412d7
    SOURCE_SUBDIR  _skip_add_subdirectory
)
FetchContent_MakeAvailable(implot)

add_library(implot_lib STATIC
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
)
target_include_directories(implot_lib PUBLIC ${implot_SOURCE_DIR})
target_link_libraries(implot_lib PUBLIC imgui_lib)

# ── ImGuiColorTextEdit via FetchContent (pinned to previously-submoduled commit) ──
FetchContent_Declare(imgui_color_text_edit
    GIT_REPOSITORY https://github.com/goossens/ImGuiColorTextEdit.git
    GIT_TAG        a74fb090d2ea9276ae6c35c2f6ab39491c7d404f
    SOURCE_SUBDIR  _skip_add_subdirectory
)
FetchContent_MakeAvailable(imgui_color_text_edit)

add_library(imgui_color_text_edit STATIC
    ${imgui_color_text_edit_SOURCE_DIR}/TextEditor.cpp
)
target_include_directories(imgui_color_text_edit PUBLIC
    ${imgui_color_text_edit_SOURCE_DIR}
)
target_link_libraries(imgui_color_text_edit PUBLIC imgui_lib)
target_compile_features(imgui_color_text_edit PUBLIC cxx_std_17)

# ── FFmpeg ────────────────────────────────────────────────────────────────
add_library(ffmpeg_lib INTERFACE)

if(WIN32)
    # Pinned to a dated (immutable) BtbN autobuild rather than the rolling
    # `latest` tag -- assets under `latest` are deleted as upstream moves on,
    # which 404s the configure step of every existing checkout. Override with
    # -DONYX_FFMPEG_WIN_URL=<zip> to test a newer build; keep the archive
    # layout (ffmpeg-*/include, /lib, /bin) the same.
    set(ONYX_FFMPEG_WIN_URL
        "https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-03-31-13-11/ffmpeg-n7.1.3-43-g5a1f107b4c-win64-gpl-shared-7.1.zip"
        CACHE STRING "FFmpeg 7.x win64 gpl-shared archive used by Onyx")

    FetchContent_Declare(
        ffmpeg_prebuilt
        URL      "${ONYX_FFMPEG_WIN_URL}"
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

# ── toml++ (header-only TOML parser for AppConfig) via FetchContent ────────
# Populate only (SOURCE_SUBDIR points at a non-existent dir); we just need the
# header include dir — toml++ is header-only.
FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        v3.4.0
    GIT_SHALLOW    TRUE
    UPDATE_DISCONNECTED TRUE
    SOURCE_SUBDIR  _skip_add_subdirectory)
FetchContent_MakeAvailable(tomlplusplus)

# ── Generated version header ──────────────────────────────────────────────
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Onyx/Version.h"
    @ONLY)

# ── Generated SF Symbols icon table ───────────────────────────────────────
# The name->glyph table the icon browser walks is derived from SFSymbols.h at
# build time rather than kept as a hand-maintained copy (which used to be 6.6k
# lines inside FontDebuggerWindow.cpp, and drifted whenever the font was
# regenerated).
set(ONYX_ICON_TABLE "${CMAKE_CURRENT_BINARY_DIR}/generated/Onyx/Fonts/SFSymbolsTable.inl")
add_custom_command(
    OUTPUT  "${ONYX_ICON_TABLE}"
    COMMAND ${CMAKE_COMMAND}
            -DHEADER=${CMAKE_CURRENT_SOURCE_DIR}/Include/Onyx/Fonts/SFSymbols.h
            -DOUTPUT=${ONYX_ICON_TABLE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateIconTable.cmake
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Include/Onyx/Fonts/SFSymbols.h"
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateIconTable.cmake"
    COMMENT "Generating SF Symbols icon table"
    VERBATIM)
set_source_files_properties("${ONYX_ICON_TABLE}" PROPERTIES
    GENERATED TRUE HEADER_FILE_ONLY TRUE)

# ── The Onyx engine library ────────────────────────────────────────────────
# Every source is assigned to exactly one layer. The lists are the single
# source of truth for the v1 target split (docs/design/2026-08-18-onyx-v1-
# architecture.md, W1); the completeness check below fails the configure if
# a file on disk belongs to no layer, so nothing can silently drop out of
# the build the way a stale explicit list would.
set(ONYX_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Audio/AdpcmDecoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Container/ChunkReader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Container/ChunkSchema.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Container/ChunkTree.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Domain/MediaKind.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Modules/DecoderRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Modules/Probe.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Modules/Workspace.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Schema/NodeInstance.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Schema/StructDef.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/AssetDatabase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/AssetVisibility.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/Diagnostics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/EventBus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/FrameScheduler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/Jobs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/Logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/Metrics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/ProfileManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/RecentFiles.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/SessionLog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/Settings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/TaskManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/Threading.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Types/TypeCatalog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Types/TypeRegistrar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Types/TypeRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Vfs/IsoFile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Vfs/IsoFileSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Vfs/OsFile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Vfs/TransformFile.cpp
)

set(ONYX_RENDER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Rendering/AnimationPlayer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Rendering/Camera.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Rendering/GpuMesh.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Rendering/GridRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Rendering/SceneRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Rendering/ShaderManager.cpp
)

set(ONYX_MEDIA_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Viewers/VideoPlayer.cpp
)

set(ONYX_SHELL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Api/ToolkitApi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/ActiveAnimation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/AnimationTimeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/App.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/AssetNodeRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/FontDebuggerWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/InfoTab.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/NativeMenuBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Panels/AnimCurveView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Panels/CameraPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Panels/Dopesheet.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Panels/IsoBrowser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Panels/PakBrowser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Panels/UiGallery.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Panels/WadStatsView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Platform/NativeWindow_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Platform/NativeWindow_windows.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Platform/SystemFileDialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Platform/Window_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Platform/Window_windows.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/SettingsWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/StatusBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/TitleBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/ViewerRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Window.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/WindowDecorator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Fonts/FontManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Fonts/IconTable.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Platform/SystemTheme_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Platform/SystemTheme_windows.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Rendering/AxisGizmo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/AppConfig.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/Appearance.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/ScaleManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/ThemeManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Viewers/DocumentWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Viewers/ImageViewer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Viewers/TextEditorViewer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Viewers/Viewport3D.cpp
)

if(APPLE)
    list(APPEND ONYX_SHELL_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Platform/NativeWindow_macos.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Platform/Window_macos.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Platform/macos_filedialog.m
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Platform/macos_menu.m
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Platform/SystemTheme_macos.mm)
endif()

# Completeness check: a .cpp on disk that no layer claims is a configure
# error, not a silent omission.
file(GLOB_RECURSE _onyx_all_cpp CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp")
set(_onyx_listed ${ONYX_CORE_SOURCES} ${ONYX_RENDER_SOURCES}
                 ${ONYX_MEDIA_SOURCES} ${ONYX_SHELL_SOURCES})
foreach(_f ${_onyx_all_cpp})
    if(NOT _f IN_LIST _onyx_listed)
        message(FATAL_ERROR "Source file not assigned to a layer: ${_f}")
    endif()
endforeach()

# Shared compile surface for every Onyx layer target. GLFW_EXPOSE_NATIVE_*
# is deliberately absent: exposing native window handles is a Shell concern.
function(onyx_apply_common tgt)
    target_compile_features(${tgt} PUBLIC cxx_std_20)
    if(MSVC)
        target_compile_options(${tgt} PUBLIC /utf-8)
    endif()
    target_compile_definitions(${tgt} PUBLIC
        $<$<PLATFORM_ID:Windows>:ONYX_OS_WINDOWS>
        $<$<PLATFORM_ID:Darwin>:ONYX_OS_MACOS>
        $<$<PLATFORM_ID:Linux>:ONYX_OS_LINUX>
        $<$<PLATFORM_ID:Windows>:NOMINMAX>)
    target_include_directories(${tgt}
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/Include
                ${CMAKE_CURRENT_BINARY_DIR}/generated
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source
                ${tomlplusplus_SOURCE_DIR}/include)
endfunction()

# ── Onyx::Core — no UI, no GPU, no GLFW ───────────────────────────────────
add_library(Onyx_Core STATIC ${ONYX_CORE_SOURCES})
add_library(Onyx::Core ALIAS Onyx_Core)
onyx_apply_common(Onyx_Core)
target_link_libraries(Onyx_Core PUBLIC glm::glm lz4_lib)

# ── Onyx::Render — draws domain data; Core + GL loader only ──────────────
add_library(Onyx_Render STATIC ${ONYX_RENDER_SOURCES})
add_library(Onyx::Render ALIAS Onyx_Render)
onyx_apply_common(Onyx_Render)
target_link_libraries(Onyx_Render PUBLIC Onyx_Core glad)

# ── Onyx::Shell — the ImGui application framework ────────────────────────
add_library(Onyx_Shell STATIC
    ${ONYX_SHELL_SOURCES}
    "${ONYX_ICON_TABLE}")
add_library(Onyx::Shell ALIAS Onyx_Shell)
onyx_apply_common(Onyx_Shell)
target_compile_definitions(Onyx_Shell PUBLIC
    $<$<PLATFORM_ID:Windows>:GLFW_EXPOSE_NATIVE_WIN32>
    $<$<PLATFORM_ID:Darwin>:GLFW_EXPOSE_NATIVE_COCOA>)
target_link_libraries(Onyx_Shell PUBLIC
    Onyx_Core
    Onyx_Render
    imgui_lib
    implot_lib
    imgui_color_text_edit
    glfw
    miniaudio)

# ── Onyx::Media — sole owner of the FFmpeg edge ──────────────────────────
# Nothing in Shell references VideoPlayer symbols (verified 2026-08-18);
# only consumers instantiate it, so OFF simply drops the code and the DLLs.
option(ONYX_COMPONENT_MEDIA "Build the video player component (FFmpeg)" ON)
if(ONYX_COMPONENT_MEDIA)
    add_library(Onyx_Media STATIC ${ONYX_MEDIA_SOURCES})
    add_library(Onyx::Media ALIAS Onyx_Media)
    onyx_apply_common(Onyx_Media)
    target_link_libraries(Onyx_Media PUBLIC Onyx_Core imgui_lib miniaudio ffmpeg_lib)
    target_link_libraries(Onyx_Shell PUBLIC Onyx_Media)
endif()

# ── Onyx::Onyx — the aggregate consumers link ─────────────────────────────
# Tests/ and GoWToolkit keep linking one name; the split stays invisible to
# them until they opt into a thinner slice (e.g. Core+Render for a CLI).
add_library(Onyx INTERFACE)
add_library(Onyx::Onyx ALIAS Onyx)
target_link_libraries(Onyx INTERFACE Onyx_Core Onyx_Render Onyx_Shell)

# ── Platform libraries, homed on the layer that needs each ───────────────
if(WIN32)
    target_link_libraries(Onyx_Render PUBLIC opengl32)
    target_link_libraries(Onyx_Shell  PUBLIC comdlg32 dwmapi)
elseif(APPLE)
    target_link_libraries(Onyx_Render PUBLIC "-framework OpenGL")
    target_link_libraries(Onyx_Shell  PUBLIC
        "-framework AudioToolbox"
        "-framework CoreAudio"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreFoundation")
else()
    target_link_libraries(Onyx_Core   PUBLIC dl pthread m)
    target_link_libraries(Onyx_Render PUBLIC GL)
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

    # ── LayerGuard: include-level layering rules ─────────────────────────
    # For each guarded layer, write its sources plus their same-name headers
    # under Include/ to a list file, then grep for forbidden includes.
    set(_lg_core "")
    foreach(_f ${ONYX_CORE_SOURCES})
        list(APPEND _lg_core "${_f}")
        string(REGEX REPLACE ".*/Source/(.*)\\.cpp$" "\\1" _rel "${_f}")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Include/Onyx/${_rel}.h")
            list(APPEND _lg_core "${CMAKE_CURRENT_SOURCE_DIR}/Include/Onyx/${_rel}.h")
        endif()
    endforeach()
    set(_lg_render "")
    foreach(_f ${ONYX_RENDER_SOURCES})
        list(APPEND _lg_render "${_f}")
        string(REGEX REPLACE ".*/Source/(.*)\\.cpp$" "\\1" _rel "${_f}")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Include/Onyx/${_rel}.h")
            list(APPEND _lg_render "${CMAKE_CURRENT_SOURCE_DIR}/Include/Onyx/${_rel}.h")
        endif()
    endforeach()
    list(JOIN _lg_core "\n" _lg_core_txt)
    list(JOIN _lg_render "\n" _lg_render_txt)
    file(WRITE "${CMAKE_BINARY_DIR}/layerguard-core.txt"   "${_lg_core_txt}")
    file(WRITE "${CMAKE_BINARY_DIR}/layerguard-render.txt" "${_lg_render_txt}")
    add_test(NAME LayerGuard.Core COMMAND ${CMAKE_COMMAND}
        -DLISTFILE=${CMAKE_BINARY_DIR}/layerguard-core.txt
        "-DFORBIDDEN=imgui|implot|GLFW/|glad/"
        -P ${CMAKE_SOURCE_DIR}/cmake/LayerGuard.cmake)
    add_test(NAME LayerGuard.Render COMMAND ${CMAKE_COMMAND}
        -DLISTFILE=${CMAKE_BINARY_DIR}/layerguard-render.txt
        "-DFORBIDDEN=imgui|implot|GLFW/"
        -P ${CMAKE_SOURCE_DIR}/cmake/LayerGuard.cmake)

    add_subdirectory(Tests)
endif()
