cmake_minimum_required(VERSION 3.20)
project(OnyxSDK VERSION 0.6.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(ONYX_BUILD_EXAMPLES "Build the Onyx example apps" ON)
option(ONYX_BUILD_TESTS    "Build the Onyx engine unit tests" ON)
# Declared here (moved up from beside the Onyx_Media target further below)
# so the FFmpeg discovery block right after this can gate on it. On
# non-Windows this option previously did nothing for FFmpeg discovery
# itself: `pkg_check_modules(FFMPEG REQUIRED ...)` ran unconditionally,
# so turning this OFF still demanded libavformat/libavcodec/... dev
# packages be installed system-wide, defeating the whole point of the
# toggle (T12: Linux CI wants to skip FFmpeg entirely rather than pull in
# apt dev packages of unpinned, possibly API-incompatible versions).
option(ONYX_COMPONENT_MEDIA "Build the video player component (FFmpeg)" ON)

include(FetchContent)
include(cmake/ShaderCompile.cmake)

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
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PUBLIC glfw)

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
# Gated on ONYX_COMPONENT_MEDIA (T12): ffmpeg_lib is only ever linked by
# Onyx_Media further below, itself built only when this option is ON --
# when it's OFF there is nothing left in the tree that needs FFmpeg found
# at all, so skip pkg-config/the Windows download entirely rather than
# demanding dev packages (or a network fetch) a Media-less build has no
# use for.
if(ONYX_COMPONENT_MEDIA)
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
endif() # ONYX_COMPONENT_MEDIA

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

# ── cgltf (header-only glTF 2.0 read/write) via FetchContent ───────────────
# Populate only (SOURCE_SUBDIR points at a non-existent dir); cgltf.h and
# cgltf_write.h are both header-only single-file libraries living at the
# repo root -- no CMakeLists of its own to add_subdirectory. v1.15 is the
# tag at HEAD of upstream's default branch as of this pin (verified via
# `git ls-remote --tags` before writing this block, plus a scratch shallow
# clone confirming both cgltf.h and cgltf_write.h exist at that tag) -- no
# substitution needed, unlike M4's volk pin (Vulkan-Headers' `1.3.296` vs
# volk's `vulkan-sdk-1.3.296.0` divergent tag scheme).
FetchContent_Declare(cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG        v1.15
    GIT_SHALLOW    TRUE
    UPDATE_DISCONNECTED TRUE
    SOURCE_SUBDIR  _skip_add_subdirectory)
FetchContent_MakeAvailable(cgltf)

# ── Vulkan (Render layer: Vulkan-Headers + volk + VMA + glslang) ──────────
# Vulkan-Headers pins the API surface every other Vulkan dep below builds
# against; declared first so volk and VMA link against these exact headers
# rather than whatever the system Vulkan SDK happens to have on PATH.
FetchContent_Declare(
    vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        v1.3.296
    GIT_SHALLOW    TRUE
    UPDATE_DISCONNECTED TRUE
)
set(VULKAN_HEADERS_ENABLE_TESTS   OFF CACHE BOOL "" FORCE)
set(VULKAN_HEADERS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
# The C++20 vulkan.hpp module target is autodetected ON for MSVC>=19.41/
# Clang>=16/GCC>=14 when the CMake version allows it -- the Render layer only uses
# volk's C API, never vulkan.hpp, and module support varies enough across
# CI's compilers that autodetection is a determinism risk not worth taking
# for a target nothing links.
set(VULKAN_HEADERS_ENABLE_MODULE  OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(vulkan_headers)

# volk's tag naming diverged from Vulkan-Headers' `vX.Y.Z` scheme -- it now
# mirrors the LunarG Vulkan SDK's dotted-quad (`vulkan-sdk-X.Y.Z.W`), and
# `1.3.296` (the tag the plan named) does not exist. `vulkan-sdk-1.3.296.0`
# is the tag matching the exact header generation as Vulkan-Headers v1.3.296
# pinned above -- same version, different spelling, not a downgrade.
FetchContent_Declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        vulkan-sdk-1.3.296.0
    GIT_SHALLOW    TRUE
    UPDATE_DISCONNECTED TRUE
)
# volk's own CMakeLists otherwise tries find_package(Vulkan)/$ENV{VULKAN_SDK}
# for headers -- OFF here and an explicit Vulkan::Headers link below keeps it
# on the exact pinned headers instead of whatever is installed system-wide.
set(VOLK_PULL_IN_VULKAN OFF CACHE BOOL "" FORCE)
set(VOLK_INSTALL        OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(volk)
target_link_libraries(volk PUBLIC Vulkan::Headers)
# PUBLIC (not just on Onyx_Render): volk.c itself must be compiled with
# VK_USE_PLATFORM_WIN32_KHR for the Win32 platform entry points (e.g.
# vkGetPhysicalDeviceWin32PresentationSupportKHR) to exist as link symbols
# at all -- a downstream-only define would leave volk.c compiled without
# them while callers expect them, an unresolved-external at link time.
target_compile_definitions(volk PUBLIC
    VK_NO_PROTOTYPES
    $<$<PLATFORM_ID:Windows>:VK_USE_PLATFORM_WIN32_KHR>)

# imgui_lib's Vulkan backend (backends/imgui_impl_vulkan.cpp, wired in
# above) resolves every Vulkan entry point through volk instead of a static
# import library -- IMGUI_IMPL_VULKAN_USE_VOLK is imgui_impl_vulkan.h's own
# configuration knob for that (see the "[Configuration] Convenience support
# for Volk" block at that header's top). PUBLIC: Source/App/Window.cpp also
# includes imgui_impl_vulkan.h directly (T9) and must see the same define,
# or its Vulkan-include branch would disagree with the one the backend was
# actually compiled against.
target_compile_definitions(imgui_lib PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
target_link_libraries(imgui_lib PUBLIC volk::volk)

# VulkanMemoryAllocator is header-only (INTERFACE target); it still needs
# Vulkan's types, so it rides on the same pinned Vulkan::Headers.
FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.1.0
    GIT_SHALLOW    TRUE
    UPDATE_DISCONNECTED TRUE
)
FetchContent_MakeAvailable(vma)
target_link_libraries(VulkanMemoryAllocator INTERFACE Vulkan::Headers)

# glslang: fetched here for T3 (runtime/offline shader compilation), not yet
# linked by anything. ENABLE_GLSLANG_BINARIES is already ON by default but
# forced explicitly since T3 depends on it existing: it builds the
# `glslang-standalone` CLI target (binary OUTPUT_NAME `glslang`). The
# library targets consumers link are `glslang::glslang` and `glslang::SPIRV`.
# Requires a Python 3 interpreter on PATH at configure time (StandAlone's
# code-gen step) -- present on this machine, noted for CI images.
FetchContent_Declare(
    glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG        14.3.0
    GIT_SHALLOW    TRUE
    UPDATE_DISCONNECTED TRUE
)
set(ENABLE_OPT              OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS           OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES ON  CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glslang)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Cli/Commands.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Container/ChunkReader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Container/ChunkSchema.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Container/ChunkTree.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Domain/MediaKind.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Modules/DecoderRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Modules/Probe.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Modules/Selection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Modules/Workspace.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Schema/NodeInstance.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Schema/StructDef.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/AssetVisibility.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/Diagnostics.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/EventBus.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/FrameScheduler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/Jobs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/Logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Services/Metrics.cpp
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

set(ONYX_TESTKIT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/TestKit/DecodeSmoke.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/TestKit/Goldens.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/TestKit/RenderCompare.cpp
)

# ── Onyx::Exchange — exporters, domain-data-out (spec §9) ────────────────
# T5 (M5): glTF export of Parsers::SceneData via cgltf. Core-only, same as
# TestKit above -- an exporter has no more business touching the renderer
# than a test harness does; the whole point (spec §9's own words) is that
# glTF opened in Blender is validation NOT written by us, independent of
# our renderer, which only holds if this layer cannot smuggle renderer
# state into the file it writes.
set(ONYX_EXCHANGE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Exchange/GltfExport.cpp
)

# ── Onyx::CliRender — CmdRender, the one Cli surface that touches the
# renderer (M5 Task 6, fixes public-surface-audit finding G1) ─────────────
# `Onyx::Cli::CmdRender` (Include/Onyx/Cli/Render.h) needs both Onyx_Core
# (Workspace/DecoderRegistry/SceneData) and Onyx_Render (VkContext,
# Pipelines, SceneRendererVk, OffscreenTarget) -- a real dependency neither
# Onyx_Core nor Onyx_Render may carry themselves: Onyx_Render already links
# Onyx_Core PUBLIC (below), so Onyx_Core linking back to reach the
# renderer would be a real CMake cycle, not just a layering smell. Before
# this task, Render.cpp's only home was compiled straight into the
# onyxbox-cli executable (Examples/OnyxCli/), which meant the public
# header's declared symbol shipped in no library at all -- see Render.h's
# own top comment for the audit's cold-start proof. This target is the
# "fourth library above both" the audit itself proposed: it depends on
# Onyx_Core and Onyx_Render, and nothing in either of them depends back on
# it, so the cycle simply cannot form.
set(ONYX_CLIRENDER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Cli/Render.cpp
)

set(ONYX_MEDIA_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/TexturePool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Viewers/VideoPlayer.cpp
)
# Source/App/TexturePool.cpp is listed in BOTH ONYX_SHELL_SOURCES and
# ONYX_MEDIA_SOURCES (T10) -- same precedent as JointPalette.cpp above: one
# physical source file, compiled independently into each target's own
# object directory. Necessary because Onyx_Shell already links Onyx_Media
# (VideoPlayer.cpp -- see that target's own comment), so Onyx_Media linking
# back to Onyx_Shell to reach a single shared TexturePool would be a real
# link cycle, not just an ugly one. Nothing is actually shared between the
# two compiled copies at runtime (each viewer owns its own TexturePool
# instance either way -- see that file's header comment), so the only
# process-wide resource in play (imgui_impl_vulkan's own descriptor pool,
# T9's DescriptorPoolSize=64) stays correctly singular regardless of which
# copy's code path calls into it.

# Task 11 ("delete the GL renderer") folded the former GL-only
# ONYX_RENDER_SOURCES into this list: Camera.cpp/AnimationPlayer.cpp move
# in from there (physical paths unchanged -- Source/Rendering/ is the
# established neutral home JointPalette.cpp and AxisGizmo.cpp already use;
# smaller diff than a `git mv`, same precedent Task 6 set), and
# RenderBatch.cpp is a genuinely new file (extracted from the deleted GL
# SceneRenderer.cpp -- see Include/Onyx/Rendering/RenderBatch.h's top
# comment). JointPalette.cpp no longer needs the dual-target compile trick
# a prior version of this comment explained: that existed only so the GL
# and Vulkan renderers could share one physical source file without
# Onyx_Render linking Onyx_Render (GL's link libs); now that Onyx_Render
# is gone, there is only one renderer target, so JointPalette.cpp has one
# normal home like everything else here.
set(ONYX_RENDER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Rendering/AnimationPlayer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Rendering/Camera.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Rendering/JointPalette.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Rendering/RenderBatch.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Rendering/RenderToImage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Rendering/OffscreenTarget.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Rendering/Pipelines.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Rendering/RenderContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Rendering/SceneRendererVk.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Rendering/TexturePool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Rendering/VkContext.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Rendering/VkResources.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Rendering/VmaImpl.cpp
)

set(ONYX_SHELL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Api/ToolkitApi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/ActiveAnimation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/AnimationTimeline.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/App.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/AssetNodeRenderer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/FontDebuggerWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/InfoTab.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/InfoTabFilter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/NativeMenuBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Panels/AnimCurveView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Panels/CameraPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Panels/DocumentBrowser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Panels/Dopesheet.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Panels/InspectorPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Panels/UiGallery.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Panels/WadStatsView.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Platform/NativeWindow_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Platform/NativeWindow_windows.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Platform/SystemFileDialog.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Platform/Window_linux.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Platform/Window_windows.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/SettingsWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/StatusBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/StatusBarFormat.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/TitleBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/ViewerOpening.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/ViewerRegistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/ViewerRouting.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/TexturePool.cpp
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
set(_onyx_listed ${ONYX_CORE_SOURCES}
                 ${ONYX_MEDIA_SOURCES} ${ONYX_SHELL_SOURCES} ${ONYX_RENDER_SOURCES}
                 ${ONYX_TESTKIT_SOURCES} ${ONYX_EXCHANGE_SOURCES} ${ONYX_CLIRENDER_SOURCES})
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

# ── Onyx::TestKit — golden snapshots, decode smoke, render compare ───────
# Task 1 (M5): ships with the SDK so every toolkit built on Onyx is "born
# testable" (spec §10) -- extracted from what this repo already grew during
# M4 (Tools/OnyxOracle/ImageCompare.{h,cpp}, the four-knob comparator) plus
# new tree-snapshot/decode-smoke helpers following Tests/cli_test.cpp's
# fixture and tree-walking patterns. See Include/Onyx/TestKit/RenderCompare.h
# for the "why Core-only, no Render" design call: none of Goldens/
# DecodeSmoke/RenderCompare ever touch a GPU or renderer type -- RenderCompare
# reads PNG files (stb_image) and does pixel-buffer math, nothing more -- so
# there is exactly one TestKit target, not a split headless/render pair: a
# headless-only toolkit never pulls in Onyx::Render by linking this at all.
add_library(Onyx_TestKit STATIC ${ONYX_TESTKIT_SOURCES})
add_library(Onyx::TestKit ALIAS Onyx_TestKit)
onyx_apply_common(Onyx_TestKit)
target_link_libraries(Onyx_TestKit PUBLIC Onyx_Core)
target_include_directories(Onyx_TestKit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)

# ── Onyx::Exchange — glTF export; Core ONLY, never the renderer ─────────
# T5 (M5, spec §9). Links Onyx_Core alone -- NOT Onyx_Render, deliberately:
# Onyx_Render already links Onyx_Core PUBLIC (below), so the reverse link
# would be a real CMake cycle (the exact reason CmdRender's own Run()
# dispatch stays in Onyx_Core via an injected RenderFn hook rather than a
# direct call -- see Include/Onyx/Cli/Render.h's top comment for the
# fully-written-out precedent this target's own placement in
# Include/Onyx/Cli/Gltf.h follows). GltfExport.cpp reads Parsers::SceneData/ObjectData
# fields directly (the same ones Source/Rendering/JointPalette.cpp reads
# to skin the mesh at draw time) and re-derives the rest-pose local TRS +
# reuses the file's own inverse-bind matrices verbatim -- see that file's
# top comment for the derivation and its citation of JointPalette.cpp as
# the oracle it must not silently diverge from.
add_library(Onyx_Exchange STATIC ${ONYX_EXCHANGE_SOURCES})
add_library(Onyx::Exchange ALIAS Onyx_Exchange)
onyx_apply_common(Onyx_Exchange)
target_link_libraries(Onyx_Exchange PUBLIC Onyx_Core)
target_include_directories(Onyx_Exchange PRIVATE
    ${cgltf_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)

# ── Onyx::Render — Vulkan 1.3 renderer; Core + volk + VMA only ───────────
# T9 links this PUBLIC into Onyx::Shell below (Window.cpp owns the
# VkContext/swapchain/RenderContext the whole app presents through). Task
# 11 deleted Onyx::Render (GL), which used to be linked alongside this one
# in every composition root (Shell, onyx-oracle, onyx_tests) -- this is now
# the only renderer target in the tree. See
# docs/design/2026-08-18-onyx-v1-architecture.md §8.
add_library(Onyx_Render STATIC ${ONYX_RENDER_SOURCES})
add_library(Onyx::Render ALIAS Onyx_Render)
onyx_apply_common(Onyx_Render)
target_compile_definitions(Onyx_Render PUBLIC
    VK_NO_PROTOTYPES
    VMA_STATIC_VULKAN_FUNCTIONS=0)
# T3's fixed camera convention (Include/Onyx/Rendering/Pipelines.h's "Camera
# convention" note): every source that builds a projection matrix via
# <glm/gtc/matrix_transform.hpp> gets Vulkan's [0,1] clip-space Z instead of
# GL's [-1,1].
#
# F3 fix-round (final review, post-Task-14): this used to be PRIVATE,
# confined to Onyx_Render's own sources (SceneRendererVk.cpp/Camera.cpp/
# RenderBatch.cpp/...), on the theory that every consumer outside this
# target (Tools/OnyxOracle/CorpusScenes.cpp+Main.cpp, Examples/OnyxCli/
# Render.cpp) built with plain glm::perspective() (GL's [-1,1] convention)
# and converted explicitly via Onyx::Rendering::VulkanProjection() before
# handing the matrix to SceneRendererVk::Render(). That theory was only
# half true: VulkanProjection() (Pipelines.h) only ever negated proj[1][1]
# (the Y-flip half of the contract) -- it never corrected the depth range,
# so every one of those outside-the-target callers fed the renderer a
# GL-depth-range matrix regardless. The GUI viewport (Camera.cpp, inside
# this target, PRIVATE-macro-corrected) and the headless corpus/CLI paths
# (outside it, never corrected) were rendering under two different clip
# conventions, silently, because nothing checked the depth half of the
# contract (SceneRendererVk.cpp's Debug assert only checks proj[1][1] < 0)
# and the parity gate's frozen GL-depth goldens happened to still match a
# GL-depth Vulkan render, canceling the bug out of that safety net too.
#
# PRIVATE was originally chosen because a PUBLIC definition, during the
# original M4 Vulkan bring-up, was caught leaking into CorpusScenes.cpp's
# own glm::perspective() calls and corrupting the GL renderer's frozen
# golden corpus -- back when onyx-oracle linked GL's Onyx::Render alongside
# this (then Vulkan-named-differently) target in the same executable. That
# GL renderer, and the live GL corpus-generation path a PUBLIC macro could
# corrupt, no longer exist (deleted at Task 11) -- Tests/Golden/corpus is a
# static, already-baked PNG set now, not something any current build
# regenerates through GL. With no GL target left for it to corrupt, PUBLIC
# is the fix that makes the contract total: every TU that links Onyx::Render
# (Onyx_Render's own sources, onyx-oracle, onyxbox-cli, onyx_tests) now
# gets Vulkan's depth convention from glm::perspective() directly, so
# Onyx::Rendering::VulkanProjection()'s existing Y-flip-only job is
# sufficient and correct everywhere it is called, with no second,
# per-call-site depth conversion needed. See Pipelines.h's "Camera
# convention" note for the full, now-total contract.
target_compile_definitions(Onyx_Render PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)
target_link_libraries(Onyx_Render PUBLIC
    Onyx_Core
    volk::volk
    Vulkan::Headers
    GPUOpen::VulkanMemoryAllocator)
# T3: compile Source/Rendering/Shaders/*.vert|*.frag to embedded SPIR-V
# headers (cmake/ShaderCompile.cmake) -- must run after the target and its
# link libraries exist (onyx_add_spirv adds an include dir + a build-order
# dependency onto Onyx_Render itself).
onyx_add_spirv(Onyx_Render
    Source/Rendering/Shaders/scene.vert
    Source/Rendering/Shaders/scene.frag
    Source/Rendering/Shaders/grid.vert
    Source/Rendering/Shaders/grid.frag
    Source/Rendering/Shaders/background.vert
    Source/Rendering/Shaders/background.frag
    Source/Rendering/Shaders/overlay.vert
    Source/Rendering/Shaders/overlay.frag)

# ── Onyx::CliRender — CmdRender; links Core + Render, nothing links back
# into it (M5 Task 6) ─────────────────────────────────────────────────────
# See ONYX_CLIRENDER_SOURCES' own comment above for why this needs to be a
# separate target at all. No LayerGuard ctest entry for the same reason
# Onyx_Exchange has none (see that target's own comment above): CMake's
# target graph already makes the forbidden cycle unbuildable -- Onyx_Core
# and Onyx_Render both link PUBLIC away from this target, never toward it,
# so there is nothing a grep-based guard would need to catch that the
# configure step would not already refuse to build.
add_library(Onyx_CliRender STATIC ${ONYX_CLIRENDER_SOURCES})
add_library(Onyx::CliRender ALIAS Onyx_CliRender)
onyx_apply_common(Onyx_CliRender)
target_link_libraries(Onyx_CliRender PUBLIC Onyx_Core Onyx_Render)
target_include_directories(Onyx_CliRender PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)

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
# option(ONYX_COMPONENT_MEDIA ...) is declared near the top of this file now
# (T12) -- the FFmpeg discovery block above needs it too, and option()
# only takes effect on its first declaration.
if(ONYX_COMPONENT_MEDIA)
    add_library(Onyx_Media STATIC ${ONYX_MEDIA_SOURCES})
    add_library(Onyx::Media ALIAS Onyx_Media)
    onyx_apply_common(Onyx_Media)
    # Onyx_Render (T10): VideoPlayer's own compiled copy of
    # Source/App/TexturePool.cpp (see ONYX_MEDIA_SOURCES' comment above)
    # needs Onyx::Rendering::Resources/VkContext to upload frames as Vulkan
    # textures -- Onyx_Media cannot link Onyx_Shell to reach the same
    # symbols (that would be the real link cycle the dual-compile avoids),
    # but linking the lower Render layer directly is cycle-free.
    target_link_libraries(Onyx_Media PUBLIC Onyx_Core Onyx_Render imgui_lib miniaudio ffmpeg_lib)
    target_link_libraries(Onyx_Shell PUBLIC Onyx_Media)
endif()

# ── Onyx::Onyx — the aggregate consumers link ─────────────────────────────
# Tests/ and GoWToolkit keep linking one name; the split stays invisible to
# them until they opt into a thinner slice (e.g. Core+Render for a CLI).
add_library(Onyx INTERFACE)
add_library(Onyx::Onyx ALIAS Onyx)
target_link_libraries(Onyx INTERFACE Onyx_Core Onyx_Render Onyx_Shell)

# ── Platform libraries, homed on the layer that needs each ───────────────
# opengl32/-framework OpenGL/GL (the GL renderer's own system link libs)
# died with Onyx_Render at Task 11 -- imgui_lib's OpenGL3 backend was
# already gone before that (imgui_impl_opengl3.cpp was never in its source
# list, only imgui_impl_glfw.cpp/imgui_impl_vulkan.cpp), so nothing in the
# tree links against a system GL library any more.
if(WIN32)
    target_link_libraries(Onyx_Shell PUBLIC comdlg32 dwmapi)
elseif(APPLE)
    target_link_libraries(Onyx_Shell PUBLIC
        "-framework AudioToolbox"
        "-framework CoreAudio"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreFoundation")
else()
    target_link_libraries(Onyx_Core PUBLIC dl pthread m)
endif()

# Provide the engine's runtime font dir to consumers
set(ONYX_FONTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts" CACHE INTERNAL "Onyx bundled fonts")

# enable_testing() must run before any add_subdirectory() that calls
# add_test() -- CMake only emits a CTestTestfile.cmake for a subdirectory
# that was added after testing was enabled in its parent scope. Hoisted here,
# ahead of Examples/Tools/OnyxOracle, rather than left inside the
# ONYX_BUILD_TESTS block below (where it used to live) so Tools/OnyxOracle's
# ctest entries (VkBootSmoke, VkOracleParity, ...; OracleGlSmoke itself
# died with the GL renderer at Task 11) register regardless of
# add_subdirectory order within this file.
enable_testing()

# Onyx_ExampleBox is unconditional (not gated by ONYX_BUILD_EXAMPLES): it is
# a tiny static lib with no UI/GPU deps, and Tests/ links it to exercise the
# module contracts (IGameModule, Workspace, DecoderRegistry) end to end.
# Added before the ONYX_BUILD_EXAMPLES block so Examples/OnyxCli below can
# link the Onyx_ExampleBox target it depends on.
add_subdirectory(Examples/OnyxBox)

if(ONYX_BUILD_EXAMPLES)
    add_subdirectory(Examples/MinimalViewer)
    add_subdirectory(Examples/OnyxCli)
endif()

# Tools/OnyxOracle -- the headless Vulkan oracle renderer (M0; GL deleted
# at Task 11). Unconditional: its ctest entries (VkBootSmoke,
# VkOracleParity, ...) are part of the standard suite, not example-gated.
add_subdirectory(Tools/OnyxOracle)

if(ONYX_BUILD_TESTS)
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
    # Task 11: this list source switched from the deleted GL-era
    # ONYX_RENDER_SOURCES to what was then ONYX_RENDERVK_SOURCES -- the
    # Render layer LayerGuard now guards is the surviving (Vulkan) one.
    # (7e58e95 later renamed ONYX_RENDERVK_SOURCES to ONYX_RENDER_SOURCES,
    # folding RenderVk into the Render layer name now that Vulkan is the
    # only renderer; both variables are named ONYX_RENDER_SOURCES at HEAD,
    # this note just disambiguates which one Task 11 actually meant.) This
    # is also why the forbidden pattern below widened to include glad/GL/:
    # the deleted GL renderer was the only code that ever legitimately
    # included them, so their reappearance in this layer would now always
    # be a regression, not just a style choice.
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
    # The swapchain lives in Shell (Window.cpp) -- GLFW must not appear in
    # Render sources either, same as before Task 11. glad/GL/ join the
    # forbidden set now that the Render layer this guards is Vulkan-only:
    # any reappearance of either is a real GL regression, not a style nit.
    add_test(NAME LayerGuard.Render COMMAND ${CMAKE_COMMAND}
        -DLISTFILE=${CMAKE_BINARY_DIR}/layerguard-render.txt
        "-DFORBIDDEN=imgui|implot|GLFW/|glad|GL/"
        -P ${CMAKE_SOURCE_DIR}/cmake/LayerGuard.cmake)
    # Onyx::Exchange (T5, M5) has no LayerGuard ctest entry of its own --
    # CMake's own target graph already makes the forbidden dependency
    # impossible to build (Onyx_Exchange links Onyx_Core only; linking
    # Onyx_Render from here would be the real cycle Include/Onyx/Cli/
    # Render.h's top comment documents, not just a style smell a grep-based
    # guard would need to catch). GltfExport.cpp's own top comment records
    # the same constraint for a human reader.

    add_subdirectory(Tests)
endif()
