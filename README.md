# OnyxSDK

Onyx is a reusable, game-agnostic engine SDK for building game-asset explorer applications. It provides windowing (GLFW), UI (Dear ImGui + ImPlot), 3D rendering (OpenGL), audio (miniaudio), video (FFmpeg), math (GLM), and compression (lz4) — all wired together as a single static library `Onyx::Onyx`.

## Consuming via CMake FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(OnyxSDK
    GIT_REPOSITORY https://github.com/JeanxPereira/OnyxSDK.git
    GIT_TAG        v0.1.0)
set(ONYX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ONYX_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(OnyxSDK)

target_link_libraries(MyApp PRIVATE Onyx::Onyx)
# On Windows, copy FFmpeg DLLs beside your exe:
# if(WIN32 AND ONYX_FFMPEG_DLLS)
#     add_custom_command(TARGET MyApp POST_BUILD
#         COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ONYX_FFMPEG_DLLS} $<TARGET_FILE_DIR:MyApp>)
# endif()
```

## Building standalone

Prerequisites: CMake 3.20+, Ninja, MSVC (Windows) or clang/GCC (Linux/macOS).

```bash
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The `MinimalViewer` example (`Examples/MinimalViewer`) demonstrates a minimal Onyx consumer that opens a window with dockable panels.
