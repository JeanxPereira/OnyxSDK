# OnyxSDK

Onyx is a reusable, game-agnostic engine SDK for building game-asset explorer applications. It provides windowing (GLFW), UI (Dear ImGui + ImPlot), 3D rendering (Vulkan 1.3, via volk + VMA), audio (miniaudio), video (FFmpeg), math (GLM), and compression (lz4) — all wired together as a single static library `Onyx::Onyx`.

## Stability policy (spec §15)

**Public surface:** everything under `Include/Onyx/**`, except any header
under an `Include/Onyx/**/Detail/` directory. `Detail/` is reserved for
headers that must physically live under `Include/` for build reasons
(template/inline bodies, platform shims a public header `#include`s) but
declare no part of the contract below — nothing under `Include/` currently
needs it, but the convention exists so a future one has a place to go
without ambiguity about whether it is covered. Everything under `Source/**`
is implementation and carries no stability promise at all; it can change
in any commit, including a patch release.

**API, not ABI.** Onyx ships as source (FetchContent/`add_subdirectory`,
see "Consuming Onyx" below) and every consumer recompiles against the
exact commit or tag they pull — there is no prebuilt binary a stale header
could silently mismatch. The stability promise below is therefore about
*source* compatibility (does your `#include` and call site still compile
and mean the same thing), never about binary layout, symbol versioning, or
struct ABI. A struct gaining a defaulted field, an enum gaining a new
enumerator, or a virtual method table changing shape are all in-bounds
changes at any version, because nothing links a prebuilt `.lib`/`.so`
against a different Onyx commit than it was compiled with.

**Pre-1.0 (current):** anything under `Include/Onyx/**` (minus `Detail/`)
may change in a breaking, source-incompatible way in a MINOR version bump
(`0.X.0`), same as any pre-1.0 SemVer project. A PATCH bump (`0.6.X`) never
breaks source compatibility on purpose. Breaking changes are called out in
`CHANGELOG.md`.

**Post-1.0:** the public surface follows SemVer's source-compatibility
reading --
  - **PATCH** (`X.Y.Z+1`): bug fixes only, no signature/behavior-contract
    change to any public declaration.
  - **MINOR** (`X.Y+1.0`): additive only -- new headers, new functions,
    new struct fields with defaults, new enumerators appended (never
    inserted) to an existing enum. Existing call sites keep compiling
    and keep meaning what they meant before.
  - **MAJOR** (`X+1.0.0`): the only version class allowed to remove,
    rename, or change the meaning of an existing public declaration.
    Reserved for real design corrections, not routine growth --
    `CHANGELOG.md` documents the migration for each one.

A header moving between two public locations (e.g. this task's
`Include/Onyx/RenderVk/` → `Include/Onyx/Rendering/` fold) is itself a
breaking, MAJOR-class change under this policy once past 1.0; pre-1.0 it
is a MINOR bump like any other breaking change, called out in
`CHANGELOG.md` same as a removed function would be.

## Building standalone

Prerequisites: CMake 3.20+, Ninja, MSVC (Windows) or clang/GCC (Linux/macOS), and a Vulkan 1.3-capable driver (the Vulkan loader/headers themselves are fetched, see below — this is about the GPU driver actually present on the machine running the build's tests or the GUI). On Linux/macOS FFmpeg comes from pkg-config; on Windows a prebuilt FFmpeg is downloaded during configure. The GUI is verified end to end on Windows only; Linux presentation is implemented and builds in CI but has never been run against a real window, and macOS is unsupported (see `CHANGELOG.md`'s "Known gaps — M4 Vulkan renderer" for why).

```bash
cmake --preset debug        # or: cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -B build
cmake --build --preset debug
ctest --test-dir build --output-on-failure
```

Everything else (GLFW, Dear ImGui, ImPlot, ImGuiColorTextEdit, GLM, lz4, toml++, doctest, Vulkan-Headers, volk, VMA, glslang) is fetched and pinned by `CMakeLists.txt` — no submodules, no vendored third-party sources beyond fonts, miniaudio and stb.

## Consuming Onyx (spec §15, audit gap G4)

**The supported model is source consumption via `FetchContent` or
`add_subdirectory` — there is no `install()`, no `export()`, and no
`find_package(OnyxSDK)` config, by design, in v1.** Onyx is built exactly
once, as part of your own project's configure, from source you pin to a
tag or commit; there is no separate "install Onyx system-wide, then
`find_package` it from unrelated projects" step, and no prebuilt
package is published. This is a deliberate scope decision, not an
oversight the SDK will "get around to" — it keeps the surface honest
about what actually exists rather than shipping a packaging story nobody
has exercised. If a future milestone adds real `install()`/`export()`
support, it will be documented here alongside the version it landed in;
until then, treat any `find_package(OnyxSDK)` invocation you might see
elsewhere as unsupported.

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

`add_subdirectory(path/to/OnyxSDK)` works the same way if you vendor the
source directly (a git submodule, a subtree, a copied checkout) instead of
using `FetchContent` — both end up calling the same root `CMakeLists.txt`.

**Targets**, all under the `Onyx::` namespace (never link the raw
`Onyx_*` names — those are implementation detail, see `Examples/`'s own
`Onyx::*`-only convention): `Onyx::Onyx` (the aggregate most consumers
want — Core + Render + Shell), `Onyx::Core` (no UI, no GPU), `Onyx::Render`
(the Vulkan renderer), `Onyx::Shell` (windowing/UI, links Core + Render),
`Onyx::Media` (VideoPlayer, only built when `ONYX_COMPONENT_MEDIA` is ON),
`Onyx::Exchange` (glTF export), `Onyx::CliRender` (headless `CmdRender`,
Core + Render with no UI dependency), `Onyx::TestKit` (opt-in golden/
decode-smoke/render-compare helpers).

**Headers** mirror that split: `#include <Onyx/Onyx.h>` is the umbrella
for everything `Onyx::Onyx`/`Onyx::Shell`/`Onyx::Core` ship (see that
file's own top comment for the exact inclusion rule); `#include
<Onyx/Render.h>` adds the Vulkan-touching renderer surface (`Onyx::Render`
alone, without the Shell); `#include <Onyx/Media.h>` adds `VideoPlayer`
when `ONYX_COMPONENT_MEDIA` is on. Splitting these three keeps a headless
consumer of `Onyx::Core`/`Onyx::CliRender` from ever having to put volk.h,
vk_mem_alloc.h, or FFmpeg's headers on its include path.

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
