# OnyxSDK

Onyx is a reusable, game-agnostic engine SDK for building game-asset explorer applications. It provides windowing (GLFW), UI (Dear ImGui + ImPlot), 3D rendering (Vulkan 1.3, via volk + VMA), audio (miniaudio), video (FFmpeg), math (GLM), and compression (lz4) — all wired together as a single static library `Onyx::Onyx`.

## Building standalone

Prerequisites: CMake 3.20+, Ninja, MSVC (Windows) or clang/GCC (Linux/macOS), and a Vulkan 1.3-capable driver (the Vulkan loader/headers themselves are fetched, see below — this is about the GPU driver actually present on the machine running the build's tests or the GUI). On Linux/macOS FFmpeg comes from pkg-config; on Windows a prebuilt FFmpeg is downloaded during configure. The GUI is verified end to end on Windows only; Linux presentation is implemented and builds in CI but has never been run against a real window, and macOS is unsupported (see `CHANGELOG.md`'s "Known gaps — M4 Vulkan renderer" for why).

```bash
cmake --preset debug        # or: cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -B build
cmake --build --preset debug
ctest --test-dir build --output-on-failure
```

Everything else (GLFW, Dear ImGui, ImPlot, ImGuiColorTextEdit, GLM, lz4, toml++, doctest, Vulkan-Headers, volk, VMA, glslang) is fetched and pinned by `CMakeLists.txt` — no submodules, no vendored third-party sources beyond fonts, miniaudio and stb.

## Consuming via CMake FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(OnyxSDK
    GIT_REPOSITORY https://github.com/JeanxPereira/OnyxSDK.git
    GIT_TAG        v0.6.0)
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

`ONYX_FFMPEG_WIN_URL` overrides the pinned Windows FFmpeg archive if you need a newer build.

## UI test mode

The SDK ships a **UI Gallery** panel: a live catalogue of everything the interface is built from, so the look can be polished without opening a single asset. Open it from **View → UI Gallery**, with **F1**, or start the example with the flag:

```bash
./build/Examples/MinimalViewer/MinimalViewer --ui-test
```

| Page | What it is for |
| --- | --- |
| Widgets | Every `Onyx::App::Widgets` wrapper beside the plain ImGui widget it replaces, in each state (normal, selected, disabled). |
| Theme | Live accent and Dark/Light/System switch, the toolbar tokens, and a per-colour **contrast audit** that paints each alpha-resolved surface with the label colour `TextForSurface` would pick and reports the WCAG ratio. |
| Typography | Font family and size, UI scale, a mono specimen for the hex/size columns, and live font metrics. |
| Icons | Searchable grid over all ~6.7k SF Symbols; click copies the `ICON_SF_*` macro name. Links to the glyph debugger for atlas/fallback problems. |
| Style | Live `ImGuiStyle` padding/rounding/border editors, with "Copy as C++" to paste the result into code. |
| Diagnostics | Frame and draw-call counters plus the Dear ImGui demo, metrics, style editor and ID-stack tools. |

Nothing the gallery changes is persisted — accent, scale, font and style edits last for the session only, so it is safe to wreck the theme while experimenting. Settings is what writes `onyx.toml`.

Any consumer gets the panel for free; to open it programmatically:

```cpp
app.setPanelVisible("UI Gallery", true);
```

## Repository layout

```
Include/Onyx/     public headers -- the SDK surface consumers include
Source/           implementation; mirrors Include/Onyx/ folder for folder
  Api/            ToolkitApi (the app-facing facade)
  App/            window shell, panels, title bar, settings, gallery
  Audio/ Container/ Domain/ Fonts/ Platform/ Rendering/ Schema/
  Services/       config, logging, tasks, theme, profiles, recents
  Types/ Vfs/ Viewers/
Examples/         MinimalViewer -- a minimal consumer of the SDK
Tests/            doctest unit tests, run by ctest
cmake/            build-time codegen (version header, SF Symbols table)
third_party/      fonts + miniaudio + stb (everything else is fetched)
docs/             design specs and plans
```

Every `Source/<X>/Foo.cpp` implements `Include/Onyx/<X>/Foo.h`; files without a public header are private to that subsystem.

## Conventions

- **Encoding**: UTF-8, no BOM, LF in the repository (`.gitattributes` enforces it).
- **Formatting**: `.clang-format` describes the house style (4-space indent, 100 columns, attached braces). The existing tree is deliberately not mass-reformatted, so `git blame` stays useful — format what you touch.
- **Generated code**: the SF Symbols name→glyph table is generated from `Include/Onyx/Fonts/SFSymbols.h` at build time (`cmake/GenerateIconTable.cmake`); reach it through `Onyx::Fonts::IconTable()` rather than hand-maintaining a copy.
