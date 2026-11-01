# Consuming Onyx

This document covers building a program on Onyx: how to bring it into a CMake project, which options switch which layers off, the three shapes a host usually takes, the runtime pieces a build must ship, and the command-line encoding contract. [`docs/architecture.md`](architecture.md) explains the layers themselves.

## Requirements

CMake 3.28 or newer, Ninja, and a C++20 compiler: MSVC 2022 on Windows, Clang 18 on Linux. The GPU layers compile their shaders at build time with `glslang`, taken from the Vulkan SDK or `PATH` when present and otherwise fetched and built by the configure. Every other dependency is fetched or vendored at configure time, except FFmpeg on Linux (see [FFmpeg](#ffmpeg)).

## Fetching

Onyx has no install rules and no package config. It is consumed as a subproject, normally through `FetchContent` pinned to a release tag:

```cmake
include(FetchContent)
FetchContent_Declare(OnyxSDK
    GIT_REPOSITORY https://github.com/JeanxPereira/OnyxSDK.git
    GIT_TAG        v2.0.0)

set(ONYX_WITH_MEDIA OFF)          # options go before MakeAvailable
FetchContent_MakeAvailable(OnyxSDK)

target_link_libraries(MyTool PRIVATE Onyx::Core Onyx::Cli)
```

Link the layers you use; each brings the layers below it. To build against a local checkout instead of the tag, configure with `-DFETCHCONTENT_SOURCE_DIR_ONYXSDK=<path>`, which is how the `Consumer.CoreOnly` test builds `Tests/Consumer/CoreOnly` against the tree it runs in.

The two example modules, the targets `Onyx::ExampleBox` and `Onyx::ExamplePrism`, are not part of any layer. A consumer that wants one, for a test or a demo, adds its directory after the fetch, as `Tests/Consumer/CoreOnly/CMakeLists.txt` does with `Examples/OnyxBox`.

## Options

| Option | Default | Off means |
|---|---|---|
| `ONYX_WITH_GPU` | ON | no `Onyx::Gpu` or `Onyx::Render`, no volk, Vulkan-Headers, VMA or glslang; also turns off the three options below |
| `ONYX_WITH_UI` | ON when GPU is on | no `Onyx::Ui` and no imgui; also turns off Shell and Media |
| `ONYX_WITH_SHELL` | ON when UI is on | no `Onyx::Shell` and no GLFW |
| `ONYX_WITH_MEDIA` | ON when UI is on | no `Onyx::Media`, no FFmpeg download or lookup, no miniaudio |
| `ONYX_BUILD_TESTS` | ON only as the top-level project | no test executables and no standalone-header checks |
| `ONYX_BUILD_EXAMPLES` | ON only as the top-level project | no example programs |

The component options default to ON even when Onyx is nested, so a consumer turns off what it does not link. Core, Cli, Exchange and TestKit are always built; Core fetches glm, toml++ and lz4, and Exchange fetches cgltf. The configure prints one `Onyx components:` line with the resulting values.

## Three shapes

### A Shell host

The program hands its modules to `Shell::Shell` and calls `Shell::Run`. One binary serves as the viewer and as the command line: a file argument opens the window, a verb runs the CLI. `Examples/MinimalViewer/Main.cpp` is the whole of such a program. Its CMake adds the two helpers a Shell host needs:

```cmake
target_link_libraries(MyViewer PRIVATE Onyx::Shell Onyx::Media)
onyx_install_assets(MyViewer)            # fonts next to the executable
onyx_install_media_runtime(MyViewer)     # FFmpeg DLLs on Windows
```

`onyx_install_assets` copies the bundled fonts into `assets/fonts` beside the executable, where the Ui layer looks for them. Both helpers come from `cmake/Assets.cmake` and are defined once `Onyx::Ui` is configured. The Shell installs only the stock verbs; to offer `render` and the `decode --to` exporters, add `Render::CliVerbs` and `Exchange::AddExporters` to `Shell::Verbs`.

### Ui inside your own window

A program that already has a window, an ImGui context and a Vulkan device links `Onyx::Ui` and configures with `ONYX_WITH_SHELL=OFF`. `Examples/Embedded` is that program. It:

1. creates its own GLFW window, Vulkan 1.3 device and ImGui context;
2. wraps the device with `Gpu::Device::Adopt`, passing a second queue of the graphics family when the family has one; when the family has only one queue, Onyx and the host share it, which is safe only because the example drives everything from one thread (its present does not take Onyx's queue mutex);
3. creates the `ImGuiVulkanBackend`, an `UploadContext` and a `TexturePool`, then a `Workspace`, the registries and the panels it wants;
4. each frame, builds a `Ui::UiContext`, draws the panels into its own dockspace, makes its frame wait on `TexturePool::Flush`, and records ImGui into the same command buffer.

When the host defines an `imgui::imgui` target before fetching Onyx, Onyx uses it instead of fetching its own, so the program never links two copies of ImGui. The Ui layer draws into the current ImGui context and never creates one. The destruction order the example follows is described under Lifetimes in [`docs/architecture.md`](architecture.md#lifetimes).

### A command-line tool

A tool that needs no window configures with `ONYX_WITH_GPU=OFF` and links `Onyx::Core`, `Onyx::Cli` and, for `decode --to`, `Onyx::Exchange`. It builds a `Workspace`, adds its modules, fills a `Cli::VerbTable` with `Cli::StockVerbs` and calls `Cli::Run`. `Examples/OnyxCli/Main.cpp` shows the pattern; it also adds `Render::CliVerbs` and therefore links `Onyx::Render`, which needs the GPU option. Drop that line and the link to stay off the graphics stack. `Tests/Consumer/CoreOnly` is the smallest consumer, `Onyx::Core` alone, and its test fails if the configure fetched anything graphical.

## Version

The configure writes `Onyx/Core/Version.h` into the Onyx build directory, under `generated/`, and `Onyx::Core` adds that directory to its include path. It defines `ONYX_VERSION_MAJOR`, `ONYX_VERSION_MINOR`, `ONYX_VERSION_PATCH` and `ONYX_VERSION_STRING`:

```cpp
#include <Onyx/Core/Version.h>
```

The header is not in the source tree, so it exists only after a configure.

## FFmpeg

`Onyx::Media` needs FFmpeg's libavformat, libavcodec, libavutil, libswscale and libswresample. A missing FFmpeg stops the configure with a message; `-DONYX_WITH_MEDIA=OFF` avoids it.

On Windows the configure downloads a pinned prebuilt archive. The URL is the cache variable `ONYX_FFMPEG_WIN_URL`, so a consumer can point it at a mirror or another FFmpeg 7 build of the same layout. The default is the LGPL shared build from BtbN, not the GPL one: Onyx is MIT and its programs redistribute the FFmpeg DLLs, which the LGPL permits for dynamically linked libraries. `onyx_install_media_runtime(<target>)` copies those DLLs next to the executable after each build; without it, a Windows program that links `Onyx::Media` fails to start. Shipping the program means shipping the DLLs and meeting the LGPL's terms for them.

On Linux FFmpeg is found through `pkg-config`, with a floor of FFmpeg 6.0 (libavformat 60). On Debian and Ubuntu:

```
sudo apt-get install libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev pkg-config
```

The system loader finds the shared libraries at run time, so `onyx_install_media_runtime` does nothing there. miniaudio is vendored in `third_party/` and built with Media on both systems.

## The build verdict outside this repository

`tools/check.py` locates the repository root from its own path and runs `cmake --preset`, the build and `ctest` there, so it always verifies the Onyx tree it belongs to. To check that a tag builds and passes on a given machine, clone it at that tag and run:

```
py -3 tools/check.py --preset windows-debug
python3 tools/check.py --preset linux-debug
```

The presets in `CMakePresets.json` (`windows-debug`, `windows-release`, `linux-debug`, `linux-release`) select Ninja, the compiler and the build type, and build into `build/<preset>`. On Windows `check.py` finds Visual Studio through `vswhere` and runs every step inside `vcvars64.bat`, so it works from a plain shell. A consumer's own project gets the same verdict by copying it to `<project>/tools/check.py`, because it takes the directory above its own as the root, with a `<project>/CMakePresets.json` that defines the same preset names; its lint step runs `tools/lint.py` and `tools/lint_test.py`, whose rules describe Onyx's layers, so a copy either brings its own lint or drops that step.

GPU and window test suites exit 77 where no Vulkan device or display exists, and `ctest` reports them as skipped; set `ONYX_NO_VALIDATION=1` where the Khronos validation layer is not installed. [`tools/README.md`](../tools/README.md) lists every suite and what it needs.

## Command-line encoding

`argv` reaching `Cli::Run` is UTF-8 on every platform, and the stock verbs build paths from it with `Services::Paths::FromUtf8` and print paths with `Services::Paths::ToUtf8`. On Linux a UTF-8 locale already delivers that. On Windows the narrow `argv` the C runtime builds is in the ANSI code page and loses any character the page cannot hold, so:

- `Shell::Run`, `Shell::RunCli` and `Shell::RouteArgs` detect that they were given the process's own arguments and read them again from `GetCommandLineW` as UTF-8. An `argv` the host built itself is used as given and must already be UTF-8.
- A program that calls `Cli::Run` directly uses `wmain` and converts each wide argument to UTF-8 before the call, as `Examples/OnyxCli/Main.cpp` does.

A verb a host adds follows the same rule: treat every argument as UTF-8 and convert it with `Services::Paths::FromUtf8` before it becomes a `std::filesystem::path`.
