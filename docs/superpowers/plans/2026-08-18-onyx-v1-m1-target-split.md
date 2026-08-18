# Onyx v1 — M1: Target Split Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split the single `Onyx` static library into `Onyx::Core` /
`Onyx::Render` / `Onyx::Shell` targets with enforced dependency edges, an
`Onyx::Onyx` aggregate for compatibility, an isolated FFmpeg component
target, and a LayerGuard test — with **zero behavior change**.

**Architecture:** Bottom-up extraction. First the GLOB is replaced by three
explicit source lists inside the existing single target (list errors surface
without split errors). Then Core, then Render, then Shell become real
targets; `Onyx` ends as an INTERFACE aggregate so `Tests/` and GoWToolkit
keep linking `Onyx::Onyx` unchanged. No file moves on disk in this
milestone — target membership is by list, physical layout is untouched.

**Tech Stack:** CMake ≥ 3.20, Ninja, MSVC (vcvars64), doctest.

**Spec:** `docs/design/2026-08-18-onyx-v1-architecture.md` (§3, §12 W1)

## Global Constraints

- **Zero behavior change.** Any bug discovered mid-task is logged in the
  task's commit body and deferred — never fixed inside this milestone.
- **No physical file moves.** Membership changes by CMake list only.
- **Commits:** Conventional Commits, subject sentence-case, ≤ 72 chars.
  **No AI attribution of any kind** — no `Co-Authored-By: Claude`, no
  "Generated with" trailers. This is a standing project rule.
- **Build command (Windows):**
  `cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul && cd /d <build-dir> && cmake .. -G Ninja && ninja'`
- **Test command:** `ctest --output-on-failure` in the build dir (SDK), and
  the GoWToolkit build uses `-DFETCHCONTENT_SOURCE_DIR_ONYXSDK=D:/CodingProjects/OnyxSDK`.
- **Entry criterion:** the in-flight logger/SessionLog branch is committed
  first; this plan's lists already include `Source/Services/SessionLog.cpp`.
- Every task ends with: SDK configure + build green, `ctest` green, commit.

## Layer manifest (the single source of truth for Tasks 1–6)

**CORE** (depends on: glm, lz4, toml++ headers — no UI, no GPU, no GLFW):

```
Source/Audio/AdpcmDecoder.cpp
Source/Container/ChunkReader.cpp
Source/Container/ChunkSchema.cpp
Source/Container/ChunkTree.cpp
Source/Domain/MediaKind.cpp
Source/Schema/NodeInstance.cpp
Source/Schema/StructDef.cpp
Source/Services/AssetDatabase.cpp
Source/Services/AssetVisibility.cpp
Source/Services/FrameScheduler.cpp
Source/Services/Logger.cpp
Source/Services/Metrics.cpp
Source/Services/ProfileManager.cpp
Source/Services/RecentFiles.cpp
Source/Services/SessionLog.cpp
Source/Services/TaskManager.cpp
Source/Services/Threading.cpp
Source/Types/TypeCatalog.cpp
Source/Types/TypeRegistry.cpp
Source/Vfs/IsoFile.cpp
Source/Vfs/IsoFileSystem.cpp
Source/Vfs/OsFile.cpp
Source/Vfs/TransformFile.cpp
```

**RENDER** (depends on: Core, glad — no imgui, no GLFW):

```
Source/Rendering/AnimationPlayer.cpp
Source/Rendering/Camera.cpp
Source/Rendering/GpuMesh.cpp
Source/Rendering/GridRenderer.cpp
Source/Rendering/SceneRenderer.cpp
Source/Rendering/ShaderManager.cpp
```

**MEDIA** (component; the only holder of the ffmpeg_lib edge):

```
Source/Viewers/VideoPlayer.cpp
```

**SHELL** (everything else; depends on Core, Render, Media, imgui/glfw/etc.):

```
Source/Api/ToolkitApi.cpp
Source/App/ActiveAnimation.cpp
Source/App/AnimationTimeline.cpp
Source/App/App.cpp
Source/App/AssetNodeRenderer.cpp
Source/App/FontDebuggerWindow.cpp
Source/App/InfoTab.cpp
Source/App/NativeMenuBar.cpp
Source/App/Panels/AnimCurveView.cpp
Source/App/Panels/CameraPanel.cpp
Source/App/Panels/Dopesheet.cpp
Source/App/Panels/IsoBrowser.cpp
Source/App/Panels/PakBrowser.cpp
Source/App/Panels/UiGallery.cpp
Source/App/Panels/WadStatsView.cpp
Source/App/Platform/NativeWindow_linux.cpp
Source/App/Platform/NativeWindow_windows.cpp
Source/App/Platform/SystemFileDialog.cpp
Source/App/Platform/Window_linux.cpp
Source/App/Platform/Window_windows.cpp
Source/App/SettingsWindow.cpp
Source/App/StatusBar.cpp
Source/App/TitleBar.cpp
Source/App/ViewerRegistry.cpp
Source/App/Widgets.cpp
Source/App/Window.cpp
Source/App/WindowDecorator.cpp
Source/Fonts/FontManager.cpp
Source/Fonts/IconTable.cpp
Source/Platform/SystemTheme_linux.cpp
Source/Platform/SystemTheme_windows.cpp
Source/Rendering/AxisGizmo.cpp
Source/Services/AppConfig.cpp
Source/Services/Appearance.cpp
Source/Services/ScaleManager.cpp
Source/Services/ThemeManager.cpp
Source/Viewers/DocumentWindow.cpp
Source/Viewers/ImageViewer.cpp
Source/Viewers/TextEditorViewer.cpp
Source/Viewers/Viewport3D.cpp
```

**SHELL (APPLE only, appended under `if(APPLE)`):**

```
Source/App/Platform/NativeWindow_macos.mm
Source/App/Platform/Window_macos.mm
Source/App/Platform/macos_filedialog.m
Source/App/Platform/macos_menu.m
Source/Platform/SystemTheme_macos.mm
```

Classification facts behind the three judgement calls (verified by include
scan on 2026-08-18): `FrameScheduler` has no non-STL includes → CORE.
`AxisGizmo.h` includes imgui (overlay drawing) → SHELL despite its
directory. `AppConfig/Appearance/ThemeManager/ScaleManager` include imgui →
SHELL until M2 replaces AppConfig with TOML settings.

---

### Task 1: Replace the GLOB with the explicit layer lists (single target unchanged)

**Files:**
- Modify: `CMakeLists.txt` (the `file(GLOB_RECURSE ONYX_SOURCES ...)` block, ~line 286)

**Interfaces:**
- Produces: CMake variables `ONYX_CORE_SOURCES`, `ONYX_RENDER_SOURCES`,
  `ONYX_MEDIA_SOURCES`, `ONYX_SHELL_SOURCES` — consumed by Tasks 2–5.

- [ ] **Step 1: Write the four list variables**

Replace the `file(GLOB_RECURSE ONYX_SOURCES ...)` block (including the
`if(APPLE)` OBJC glob) with `set(ONYX_CORE_SOURCES ...)`,
`set(ONYX_RENDER_SOURCES ...)`, `set(ONYX_MEDIA_SOURCES ...)`,
`set(ONYX_SHELL_SOURCES ...)` containing exactly the manifest above, each
path prefixed `${CMAKE_CURRENT_SOURCE_DIR}/`. Keep the APPLE additions as:

```cmake
if(APPLE)
    list(APPEND ONYX_SHELL_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Platform/NativeWindow_macos.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Platform/Window_macos.mm
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Platform/macos_filedialog.m
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/App/Platform/macos_menu.m
        ${CMAKE_CURRENT_SOURCE_DIR}/Source/Platform/SystemTheme_macos.mm)
endif()
```

Then feed the existing target from the union, so this task changes nothing
else:

```cmake
add_library(Onyx STATIC
    ${ONYX_CORE_SOURCES} ${ONYX_RENDER_SOURCES}
    ${ONYX_MEDIA_SOURCES} ${ONYX_SHELL_SOURCES}
    "${ONYX_ICON_TABLE}")
```

- [ ] **Step 2: Guard against drift from the tree**

Immediately after the lists, add a configure-time completeness check so a
new `.cpp` on disk that is in no list fails the configure instead of
silently vanishing from the build:

```cmake
file(GLOB_RECURSE _onyx_all_cpp CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp")
set(_onyx_listed ${ONYX_CORE_SOURCES} ${ONYX_RENDER_SOURCES}
                 ${ONYX_MEDIA_SOURCES} ${ONYX_SHELL_SOURCES})
foreach(_f ${_onyx_all_cpp})
    if(NOT _f IN_LIST _onyx_listed)
        message(FATAL_ERROR "Source file not assigned to a layer: ${_f}")
    endif()
endforeach()
```

- [ ] **Step 3: Configure + build + test**

Run the build command from Global Constraints, then `ctest
--output-on-failure`. Expected: configure passes (every file assigned),
build and tests identical to before.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: Assign every source to an explicit layer list"
```

### Task 2: Extract Onyx_Core as a real target

**Files:**
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: target `Onyx_Core` (alias `Onyx::Core`) carrying PUBLIC include
  dirs `Include/` + `generated/`, PUBLIC deps `glm::glm`, `lz4_lib`, the
  `/utf-8` + `ONYX_OS_*`/`NOMINMAX` compile definitions, and `cxx_std_20`.
  Tasks 3–5 link against it and inherit all of that transitively.

- [ ] **Step 1: Create a helper for the shared compile settings**

Above the target definitions:

```cmake
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
```

(`GLFW_EXPOSE_NATIVE_*` stays out of the helper — it is a Shell concern and
moves there in Task 4.)

- [ ] **Step 2: Define the Core target and re-point Onyx**

```cmake
add_library(Onyx_Core STATIC ${ONYX_CORE_SOURCES})
add_library(Onyx::Core ALIAS Onyx_Core)
onyx_apply_common(Onyx_Core)
target_link_libraries(Onyx_Core PUBLIC glm::glm lz4_lib)
```

Remove `${ONYX_CORE_SOURCES}` from the `Onyx` target's sources and add
`target_link_libraries(Onyx PUBLIC Onyx_Core)`. Remove `glm::glm` and
`lz4_lib` from Onyx's own link list (now inherited).

- [ ] **Step 3: Build + test + fix illegal includes only**

Build. If a Core translation unit fails because it includes a Shell-layer
header, that is a discovered coupling: fix it **by cutting the include**
(forward declaration, or moving the offending declaration to an existing
Core header) — never by moving the file back to Shell without recording
why in the commit body. Expected from the include scan: Core compiles clean.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "build(core): Extract Onyx_Core with glm+lz4 edges only"
```

### Task 3: Extract Onyx_Render

**Files:**
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Onyx_Core` (Task 2).
- Produces: target `Onyx_Render` (alias `Onyx::Render`) — PUBLIC deps
  `Onyx_Core`, `glad`.

- [ ] **Step 1: Define the target**

```cmake
add_library(Onyx_Render STATIC ${ONYX_RENDER_SOURCES})
add_library(Onyx::Render ALIAS Onyx_Render)
onyx_apply_common(Onyx_Render)
target_link_libraries(Onyx_Render PUBLIC Onyx_Core glad)
```

Remove `${ONYX_RENDER_SOURCES}` from `Onyx`; add `Onyx_Render` to Onyx's
links; remove `glad` from Onyx's own list.

- [ ] **Step 2: Build + test**

Same criteria as Task 2 Step 3. Watch for: a Render TU including imgui or
GLFW would fail here — `AxisGizmo.cpp` must NOT be in the Render list (it
is Shell in the manifest).

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build(render): Extract Onyx_Render on Core+glad only"
```

### Task 4: Extract Onyx_Shell and turn Onyx into the aggregate

**Files:**
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Onyx_Core`, `Onyx_Render`.
- Produces: target `Onyx_Shell` (alias `Onyx::Shell`); `Onyx` becomes
  `INTERFACE` linking all three; `Onyx::Onyx` alias unchanged for Tests/
  and GoWToolkit.

- [ ] **Step 1: Define Shell and demote Onyx to INTERFACE**

```cmake
add_library(Onyx_Shell STATIC ${ONYX_SHELL_SOURCES} ${ONYX_MEDIA_SOURCES}
            "${ONYX_ICON_TABLE}")
add_library(Onyx::Shell ALIAS Onyx_Shell)
onyx_apply_common(Onyx_Shell)
target_compile_definitions(Onyx_Shell PUBLIC
    $<$<PLATFORM_ID:Windows>:GLFW_EXPOSE_NATIVE_WIN32>
    $<$<PLATFORM_ID:Darwin>:GLFW_EXPOSE_NATIVE_COCOA>)
target_link_libraries(Onyx_Shell PUBLIC
    Onyx_Core Onyx_Render
    imgui_lib implot_lib imgui_color_text_edit glfw miniaudio ffmpeg_lib)

add_library(Onyx INTERFACE)
add_library(Onyx::Onyx ALIAS Onyx)
target_link_libraries(Onyx INTERFACE Onyx_Core Onyx_Render Onyx_Shell)
```

(The old `add_library(Onyx STATIC ...)` block and its target_* calls are
deleted; `${ONYX_MEDIA_SOURCES}` rides in Shell until Task 5 relocates it.)

- [ ] **Step 2: Re-home the platform link libraries**

Move the `if(WIN32)/elseif(APPLE)/else()` platform block onto the targets
that need each library: `opengl32` (and Apple `-framework OpenGL`, Linux
`GL`) onto `Onyx_Render`; `comdlg32 dwmapi` (and the Cocoa/AudioToolbox
frameworks, Linux `dl pthread m`) onto `Onyx_Shell`. `Onyx_Core` gets none.

- [ ] **Step 3: Build + test the SDK**

Build; run `ctest --output-on-failure`. Tests link `Onyx::Onyx` and must
pass unchanged.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "build(shell): Extract Onyx_Shell; Onyx::Onyx becomes the aggregate"
```

### Task 5: Isolate the FFmpeg edge in an Onyx_Media component

**Files:**
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `Onyx_Shell` (Task 4).
- Produces: target `Onyx_Media` (alias `Onyx::Media`) — sole owner of the
  `ffmpeg_lib` link; option `ONYX_COMPONENT_MEDIA` (default `ON`).

- [ ] **Step 1: Define the component and detach ffmpeg from Shell**

```cmake
option(ONYX_COMPONENT_MEDIA "Build the video player component (FFmpeg)" ON)
if(ONYX_COMPONENT_MEDIA)
    add_library(Onyx_Media STATIC ${ONYX_MEDIA_SOURCES})
    add_library(Onyx::Media ALIAS Onyx_Media)
    onyx_apply_common(Onyx_Media)
    target_link_libraries(Onyx_Media PUBLIC Onyx_Core imgui_lib ffmpeg_lib)
    target_link_libraries(Onyx_Shell PUBLIC Onyx_Media)
endif()
```

Remove `${ONYX_MEDIA_SOURCES}` and `ffmpeg_lib` from `Onyx_Shell`.

- [ ] **Step 2: Verify both option states**

Build with the default (`ON`): identical behavior. Then configure a scratch
build dir with `-DONYX_COMPONENT_MEDIA=OFF` and build **the SDK only**. If
Shell code references `VideoPlayer` symbols directly, the OFF build fails —
in that case wrap only the referencing lines in
`#ifdef ONYX_HAS_MEDIA` and add
`target_compile_definitions(Onyx_Shell PUBLIC ONYX_HAS_MEDIA)` inside the
`if(ONYX_COMPONENT_MEDIA)` block. GoWToolkit is built with `ON` in M1;
consumer-side opt-out arrives with `App::Use()` in M3.

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build(media): Give the FFmpeg edge its own component target"
```

### Task 6: LayerGuard test

**Files:**
- Create: `cmake/LayerGuard.cmake`
- Modify: `CMakeLists.txt` (register the test under `if(ONYX_BUILD_TESTS)`)

**Interfaces:**
- Consumes: the four source-list variables (Task 1).
- Produces: ctest `LayerGuard`.

- [ ] **Step 1: Write the guard script**

`cmake/LayerGuard.cmake` — scans each layer's sources (and each source's
same-name header under `Include/`, when one exists) for includes that are
illegal in that layer:

```cmake
# LayerGuard.cmake — invoked as:
#   cmake -DLISTFILE=<file with one path per line> -DFORBIDDEN=<regex> -P LayerGuard.cmake
file(STRINGS "${LISTFILE}" _files)
set(_bad "")
foreach(_f ${_files})
    if(NOT EXISTS "${_f}")
        continue()
    endif()
    file(STRINGS "${_f}" _hits REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"](${FORBIDDEN})")
    if(_hits)
        list(APPEND _bad "${_f}: ${_hits}")
    endif()
endforeach()
if(_bad)
    list(JOIN _bad "\n" _msg)
    message(FATAL_ERROR "Layer violation(s):\n${_msg}")
endif()
```

- [ ] **Step 2: Register two tests at configure time**

In the tests section of `CMakeLists.txt`, write each layer's file list
(sources + the matching `Include/Onyx/...` headers derived by name) to
`${CMAKE_BINARY_DIR}/layerguard-core.txt` / `-render.txt` with
`file(WRITE ...)`, then:

```cmake
add_test(NAME LayerGuard.Core COMMAND ${CMAKE_COMMAND}
    -DLISTFILE=${CMAKE_BINARY_DIR}/layerguard-core.txt
    -DFORBIDDEN=imgui|implot|GLFW/|glad/ -P ${CMAKE_SOURCE_DIR}/cmake/LayerGuard.cmake)
add_test(NAME LayerGuard.Render COMMAND ${CMAKE_COMMAND}
    -DLISTFILE=${CMAKE_BINARY_DIR}/layerguard-render.txt
    -DFORBIDDEN=imgui|implot|GLFW/ -P ${CMAKE_SOURCE_DIR}/cmake/LayerGuard.cmake)
```

- [ ] **Step 3: Prove the guard catches a violation**

Temporarily add `#include <imgui.h>` to `Source/Services/Metrics.cpp`, run
`ctest -R LayerGuard`, expect **FAIL** naming the file. Revert the line,
rerun, expect PASS. (This is the test's own failing-test step.)

- [ ] **Step 4: Commit**

```bash
git add cmake/LayerGuard.cmake CMakeLists.txt
git commit -m "test: Add LayerGuard enforcing Core/Render include rules"
```

### Task 7: Consumer gate — GoWToolkit on the split SDK

**Files:**
- Modify: `CHANGELOG.md` (SDK)
- No GoWToolkit source changes expected — that is the point.

- [ ] **Step 1: Full SDK verification**

Clean-configure a fresh build dir; build; `ctest --output-on-failure` — all
green, including LayerGuard.

- [ ] **Step 2: Build GoWToolkit against the local split SDK**

In `D:/CodingProjects/GoWToolkit/build-ninja` (which already carries
`FETCHCONTENT_SOURCE_DIR_ONYXSDK=D:/CodingProjects/OnyxSDK`): re-run cmake +
ninja. Expected: builds with zero GoWToolkit changes; its test suite passes
at the same count as before the split.

- [ ] **Step 3: Runtime eyeball**

Ask Jean to launch GoWToolkit, open a GOWR wad and a GOW2 iso, open a mesh
in the viewport, play a clip, open a video. M1 changes nothing visible —
anything visible is a regression.

- [ ] **Step 4: Changelog + commit**

Add under Unreleased: "build: Onyx split into Core/Render/Shell targets
(+Media component); `Onyx::Onyx` remains the aggregate consumers link."

```bash
git add CHANGELOG.md
git commit -m "docs: Record the Core/Render/Shell target split"
```

---

## Self-review notes

- Spec coverage (W1): three targets ✔ (T2–T4), aggregate compat ✔ (T4),
  component skeleton ✔ (T5), illegal-include enforcement ✔ (T6), "GoWToolkit
  runs on the split targets" gate ✔ (T7). Intra-Shell layering
  (Viewers→App/Panels include direction) is *not* W1 scope — both live in
  Shell; recorded in the spec as later hygiene.
- The manifest classifications were verified by include scan on 2026-08-18;
  if a build failure contradicts the manifest, the failure wins — cut the
  include or reclassify, and say which in the commit body.
- Type consistency: the four list variables introduced in T1 are the same
  names consumed in T2–T6.
