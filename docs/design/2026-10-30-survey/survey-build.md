# OnyxSDK survey: build, deps, CI, tests, docs, tooling

Scope: read-only survey of `D:\CodingProjects\OnyxSDK`. Working tree HEAD is `feat/host-ui-hooks` at local commit `6dd2be6` (2026-09-19). Facts only.

Important snapshot caveat: the **remote** (`origin` = github.com/JeanxPereira/OnyxSDK) is ahead of the local clone. The remote has tags `v1.3.1`, `v1.3.2`, `v1.3.3` and CI runs dated up to 2026-10-16. The local clone has tags only up to `v1.2.0`. Everything below describes the local tree unless marked "remote".

---

## 1. Build system

### 1.1 Root and sub-CMake files

| File | Lines | Role |
|---|---|---|
| `CMakeLists.txt` (root) | 923 | Fetches all deps, defines all library targets, LayerGuard tests, adds subdirs |
| `CMakePresets.json` | 27 | Two presets only: `debug` (build/), `release` (build-release/), both Ninja |
| `cmake/ShaderCompile.cmake` | 60 | `onyx_add_spirv()`: glslang-compiles GLSL to SPIR-V headers at build time |
| `cmake/GenerateSpirvHeader.cmake` | 80 | Emits `constexpr uint32_t` arrays from `.spv` |
| `cmake/GenerateIconTable.cmake` | 22 | Builds `SFSymbolsTable.inl` from `Include/Onyx/Fonts/SFSymbols.h` at build time |
| `cmake/LayerGuard.cmake` | 23 | Grep-based include-layer check, invoked by two ctest entries |
| `cmake/Version.h.in` | 21 | Template for `Include/Onyx/Version.h` |
| `Examples/OnyxBox/CMakeLists.txt` | 10 | `Onyx_ExampleBox` STATIC |
| `Examples/MinimalViewer/CMakeLists.txt` | 34 | EXE, GLOB of `Source/*.cpp` (3 TUs) |
| `Examples/OnyxCli/CMakeLists.txt` | 94 | EXE `onyxbox-cli` (2 TUs) + 3 render ctest entries |
| `Tools/ColdStart/CMakeLists.txt` | 35 | EXE `ColdStartToolkit` (1 TU), links `Onyx::Onyx` only |
| `Tools/OnyxOracle/CMakeLists.txt` | 176 | EXE `onyx-oracle` (6 TUs) + 7 ctest entries + `ONYX_PARITY_ARGS` cache var |
| `Tests/CMakeLists.txt` | 122 | EXE `onyx_tests` (44 TUs incl. 3 from Tools/OnyxOracle) + 42 ctest entries |
| `Tests/Consumer/CMakeLists.txt` | 38 | `ExternalConsumption` ctest entry |
| `Tests/Consumer/consumer_project/CMakeLists.txt` | 52 | Fake external consumer project |

Other CMake helper files: `Examples/OnyxCli/{RenderTest,RenderViewsTest,RenderStrictTest}.cmake`, `Tools/OnyxOracle/{ReproTest,VkParityTest}.cmake`, `Tests/Consumer/ExternalConsumptionTest.cmake`.

### 1.2 Target table (all targets in the root build)

TU counts are `.cpp` (and `.c`/`.mm`/`.m` where noted). Link deps are as written in CMake (PUBLIC unless marked).

| Target (alias) | Type | TUs | Link deps | Notes |
|---|---|---|---|---|
| `lz4_lib` | STATIC | 4 (C) | none | lz4.c, lz4frame.c, lz4hc.c, xxhash.c |
| `miniaudio` | STATIC | 1 (C) | none | vendored single-header impl |
| `imgui_lib` | STATIC | 7 | `glfw`, `volk::volk` | includes `imgui_demo.cpp` in the library; Vulkan + GLFW backends |
| `implot_lib` | STATIC | 2 | `imgui_lib` | |
| `imgui_color_text_edit` | STATIC | 1 | `imgui_lib`; C++17 feature | |
| `ffmpeg_lib` | INTERFACE | 0 | Windows: 5 `.lib` from prebuilt zip; Linux: pkg-config | only if `ONYX_COMPONENT_MEDIA` |
| `Onyx_Core` (`Onyx::Core`) | STATIC | 31 | `glm::glm`, `lz4_lib`; Linux also `dl pthread m` | no UI, no GPU (per CMake comment) |
| `Onyx_TestKit` (`Onyx::TestKit`) | STATIC | 3 | `Onyx_Core` | golden/decode/render-compare helpers; stb_image |
| `Onyx_Exchange` (`Onyx::Exchange`) | STATIC | 1 | `Onyx_Core`; cgltf include dir | glTF export only |
| `Onyx_Render` (`Onyx::Render`) | STATIC | 13 + 8 generated SPIR-V headers | `Onyx_Core`, `volk::volk`, `Vulkan::Headers`, `GPUOpen::VulkanMemoryAllocator` | defines `VK_NO_PROTOTYPES`, `VMA_STATIC_VULKAN_FUNCTIONS=0`, `GLM_FORCE_DEPTH_ZERO_TO_ONE` PUBLIC |
| `Onyx_CliRender` (`Onyx::CliRender`) | STATIC | 1 | `Onyx_Core`, `Onyx_Render` | `CmdRender` |
| `Onyx_Shell` (`Onyx::Shell`) | STATIC | 45 + generated `SFSymbolsTable.inl` | `Onyx_Core`, `Onyx_Render`, `imgui_lib`, `implot_lib`, `imgui_color_text_edit`, `glfw`, `miniaudio`, `Onyx_Media` (if on); Windows: `ole32 uuid shell32 comdlg32 dwmapi` | the whole ImGui app framework |
| `Onyx_Media` (`Onyx::Media`) | STATIC | 2 | `Onyx_Core`, `Onyx_Render`, `imgui_lib`, `miniaudio`, `ffmpeg_lib` | only if `ONYX_COMPONENT_MEDIA=ON` (default ON); its own copy of `TexturePool.cpp` (same file also in Shell) |
| `Onyx` (`Onyx::Onyx`) | INTERFACE | 0 | `Onyx_Core`, `Onyx_Render`, `Onyx_Shell` | aggregate umbrella |
| `Onyx_ExampleBox` | STATIC | 1 | `Onyx::Core` | built unconditionally (not gated by ONYX_BUILD_EXAMPLES) |
| `MinimalViewer` | EXE | 3 | `Onyx::Onyx`, `Onyx::Render`, `Onyx_ExampleBox` | example, gated by ONYX_BUILD_EXAMPLES |
| `onyxbox-cli` | EXE | 2 | `Onyx::Core`, `Onyx_ExampleBox`, `Onyx::CliRender`, `Onyx::Exchange` | example |
| `onyx-oracle` | EXE | 6 | `Onyx::Render`, `Onyx::TestKit` | **built unconditionally** (Tools/) |
| `ColdStartToolkit` | EXE | 1 | `Onyx::Onyx` only | **built unconditionally** (Tools/) |
| `onyx_tests` | EXE | 44 (41 in Tests/ + 3 in Tools/OnyxOracle) | `Onyx::Onyx`, `Onyx::Render`, `Onyx::TestKit`, `Onyx::Exchange`, `Onyx_ExampleBox`, `doctest`, `imgui_lib` | only if `ONYX_BUILD_TESTS` |
| `consumer_app` (nested) | EXE | 1 | `Onyx::Onyx` | only inside `ExternalConsumption` ctest, sub-configure |

Source totals: `Source/` has 95 `.cpp` + 5 Objective-C (`.mm`/`.m`, macOS only) + 12 private `.h`. `Include/` has 117 public headers. Sum of the layer lists in the root CMake = 31 + 3 + 1 + 1 + 2 + 13 + 45 = 96 files; the completeness check (`file(GLOB_RECURSE ...)` on `Source/*.cpp` vs the lists) makes configure fail if a `.cpp` is unlisted. The 5 `.mm/.m` files are listed only under `if(APPLE)`.

### 1.3 Is Onyx one library or split?

**Split per layer**, 7 static libraries plus the aggregate: `Onyx_Core`, `Onyx_TestKit`, `Onyx_Exchange`, `Onyx_Render`, `Onyx_CliRender`, `Onyx_Shell`, `Onyx_Media` (optional), plus `Onyx` INTERFACE alias. The root comment states this is the v1 target split (`docs/design/2026-08-18-onyx-v1-architecture.md`, W1).

**Can a consumer link only VFS + decoders without ImGui/Vulkan?**
- At CMake level: yes. `Onyx::Core` links only `glm` and `lz4` (plus `dl pthread m` on Linux). A consumer linking `Onyx::Core` gets no ImGui, Vulkan, or GLFW link dependency.
- At header level, mostly yes. A scan of the transitive include closure of the 31 `Onyx_Core` TUs (own static scan, not a compile) finds **no** ImGui, GLFW, Vulkan, VMA, or GL include. Six Core TUs have leaks:
  - `Source/Domain/MediaKind.cpp`, `Source/Types/TypeCatalog.cpp`, `Source/Types/TypeRegistry.cpp`, `Source/Services/AssetVisibility.cpp` transitively include `Include/Onyx/Fonts/SFSymbols.h` (6686 lines) through `Include/Onyx/Types/ITypeHandler.h`, which includes it directly.
  - `Source/Services/SessionLog.cpp` includes `<windows.h>` (Windows-only, in a Core TU).
  - `Source/Services/Settings.cpp` includes `toml++/toml.hpp`. The toml++ include dir is PRIVATE to the Core target, so the .cpp compiles, but Core depends on toml++ at compile time.
- Not verified by building: whether every Core symbol resolves with only `Onyx::Core` linked.
- Consumers still pay for every FetchContent (see section 2). Linking only `Onyx::Core` does not avoid downloading and building GLFW, ImGui, glslang, Vulkan-Headers, VMA, and the Windows FFmpeg zip, because the root CMake fetches them unconditionally. Only FFmpeg (behind `ONYX_COMPONENT_MEDIA`) and doctest (behind `ONYX_BUILD_TESTS`) are gated.
- `Onyx::Render` requires volk, Vulkan-Headers, VMA, and builds 8 shaders through glslang at build time.

### 1.4 Other build observations

- `ONYX_BUILD_TESTS=OFF` still builds `Tools/OnyxOracle` (onyx-oracle), `Tools/ColdStart`, and `Examples/OnyxBox`. Only `Tests/` and the LayerGuard entries are gated.
- The root sets `ENV{VSLANG}=1033` before `project()` to force English MSVC diagnostics (CMake comment: a localized prefix silently breaks Ninja dependency parsing).
- `CMAKE_POLICY_VERSION_MINIMUM 3.5` is set just for the doctest fetch, then unset.
- `configure_file(cmake/Version.h.in -> Include/Onyx/Version.h)` writes a **tracked source-tree file** on every configure. See section 10.
- The root `file(REMOVE build/generated/Onyx/Version.h)` deletes a stale copy from older build dirs.
- `enable_testing()` is hoisted above `add_subdirectory(Tools/...)`.
- `GLFW_EXPOSE_NATIVE_WIN32` / `COCOA` are PUBLIC on `Onyx_Shell`. `ONYX_OS_*` and `NOMINMAX` are PUBLIC on every layer via `onyx_apply_common()`.
- `Onyx_Core` `/utf-8` on MSVC via `onyx_apply_common`.
- `ONYX_PARITY_ARGS` is one cache string holding the four-tier VkOracleParity tolerance (`--max-channel-delta 160 --max-differing-pct 2.0 --max-high-delta-pct 0.8 --max-mae 1.0`).

---

## 2. Dependencies

### 2.1 FetchContent (configure-time download)

| Dependency | Pin | Pin type | Size category | Needed by |
|---|---|---|---|---|
| GLFW | 3.3.9 | git tag, shallow | medium (compiled C) | Onyx_Shell, imgui_lib (always fetched, even for Core-only consumers) |
| GLM | 1.0.1 | git tag, shallow | header-only | Onyx_Core (PUBLIC) |
| lz4 | v1.9.4 | git tag, shallow | small (C) | lz4_lib -> Onyx_Core |
| Dear ImGui | `2af6dd96...` (commit) | commit SHA | medium (C++) | imgui_lib |
| ImPlot | `d65a2bef...` (commit) | commit SHA | small | implot_lib |
| ImGuiColorTextEdit | `a74fb090...` (commit) | commit SHA | small | imgui_color_text_edit |
| FFmpeg (Windows) | `autobuild-2026-03-31-13-11`, `ffmpeg-n7.1.3-43-g5a1f107b4c-win64-gpl-shared-7.1.zip` | dated URL, **no URL_HASH** | large prebuilt binary zip (DLLs + libs) | ffmpeg_lib, only if ONYX_COMPONENT_MEDIA |
| FFmpeg (Linux) | system pkg-config, **unpinned** | none | system | ffmpeg_lib, only if ONYX_COMPONENT_MEDIA |
| toml++ | v3.4.0 | git tag, shallow | header-only | Onyx_Core (private include), Shell |
| cgltf | v1.15 | git tag, shallow | header-only | Onyx_Exchange, onyx_tests |
| Vulkan-Headers | v1.3.296 | git tag, shallow | header-only | Onyx_Render (and volk, VMA) |
| volk | `vulkan-sdk-1.3.296.0` | git tag, shallow | small (C) | Onyx_Render, imgui_lib |
| VulkanMemoryAllocator | v3.1.0 | git tag, shallow | header-only | Onyx_Render |
| glslang | 14.3.0 | git tag, shallow | **large**: builds the glslang compiler + SPIRV lib + standalone CLI (`glslang-standalone`); needs Python 3 at configure | onyx_add_spirv (every Render build), 8 shaders |
| doctest | v2.4.11 | git tag, shallow | header-only | onyx_tests (only if ONYX_BUILD_TESTS) |

Most declarations use `GIT_SHALLOW TRUE` + `UPDATE_DISCONNECTED TRUE`. The three commit-pinned ones (ImGui, ImPlot, ImGuiColorTextEdit) have neither, so they re-clone on a fresh configure and their `SOURCE_SUBDIR` is `_skip_add_subdirectory`. FFmpeg is a URL download with no `URL_HASH`. Nothing in the root carries a checksum. No `find_package` path exists for any of them.

### 2.2 Vendored (checked into git)

| Path | Size (du) | What |
|---|---|---|
| `third_party/miniaudio/` | 4.1 MB (miniaudio.h ~4.1 MB, .c) | miniaudio single-header |
| `third_party/fonts/` | 1.9 MB | `SFSymbols.ttf` (1.7 MB), `SFMono-Regular.otf` (92 KB) |
| `third_party/stb/` | 352 KB | stb_image.h, stb_image_write.h |
| `Include/Onyx/Fonts/SFSymbols.h` | 384 KB, 6686 lines | generated-like symbol header; its table is generated at build time |

README's Dependencies section says "nothing vendored beyond fonts, miniaudio and stb." That matches.

### 2.3 Other external requirements (not fetched)

- Windows: MSVC (`/utf-8`), system libs `ole32 uuid shell32 comdlg32 dwmapi`.
- Linux: `dl pthread m`; X11 dev headers for GLFW (`xorg-dev` in CI); FFmpeg dev packages via pkg-config (not in CI, Media off).
- Python 3 on PATH at configure (glslang).
- A Vulkan 1.3 driver at runtime for render tests.

### 2.4 Unpinned or weakly pinned items

- Linux FFmpeg: pkg-config, no version.
- Python 3: any version.
- ImGui, ImPlot, ImGuiColorTextEdit: pinned to commit SHAs, not tags. Acceptable, but no upstream release identity.
- Windows FFmpeg zip: pinned by URL, not by hash.
- The FFmpeg URL is a dated autobuild from a third-party (BtbN) GitHub release. A deleted release 404s the configure step (the CMake comment says so explicitly).

---

## 3. Compile weight

### 3.1 TU counts (summary)

Sum of per-target TU counts from section 1.2 (ExternalConsumption's nested build excluded): Core 31 + TestKit 3 + Exchange 1 + Render 13 + CliRender 1 + Shell 45 + Media 2 + ExampleBox 1 + MinimalViewer 3 + onyxbox-cli 2 + onyx-oracle 6 + ColdStart 1 + onyx_tests 44 = 153 compile units. Some are compiled twice on purpose (`TexturePool.cpp` in Shell and Media; `Tools/OnyxOracle/{CorpusScenes,CorpusTextures,RenderReport}.cpp` in onyx-oracle and onyx_tests), and the C files in lz4/miniaudio are counted in their targets. Per-target counts are in section 1.2.

Shell (45 TUs) is the largest compile unit, and `onyx_tests` (44 TUs) is second.

### 3.2 PCH and unity

- No `target_precompile_headers` in any CMake file.
- No `UNITY_BUILD` anywhere (checked `CMakeLists.txt`, `*.cmake`, `CMakePresets.json`).
- Consequence: every Shell TU re-parses the whole ImGui/GLFW/Onyx header closure. The header closure is large (see 3.3).

### 3.3 Include graph (static scan)

Method: `scratchpad/incl_graph.py` resolves `#include` lines against `Include/`, `Source/`, `third_party/stb`, `third_party/miniaudio`, and `Tools/OnyxOracle`/`Examples/OnyxBox`, then takes the transitive closure per TU. Third-party includes (imgui.h, glm, volk, GLFW, Windows headers) are **not** counted, so real TU costs are higher.

- 150 `.cpp` TUs scanned.
- Onyx-owned headers per TU: median 4, max 82 (`Tests/Consumer/consumer_project/main.cpp` and `Tools/ColdStart/ColdStartOnyx.cpp`, both including `<Onyx/Onyx.h>`), then `Source/App/App.cpp` 63, `Examples/MinimalViewer/Source/Main.cpp` 53, `Source/App/Window.cpp` 52, `Source/App/Panels/DocumentBrowser.cpp` 39, `Window_linux.cpp` 39, `Window_windows.cpp` 39.
- Umbrella use: `<Onyx/Onyx.h>` is included by 12 files.

**Header fan-in** (TUs transitively including the header; top ones):

| Fan-in | Header |
|---|---|
| 52 | `Include/Onyx/Parsers/ObjectData.h` |
| 52 | `Include/Onyx/Parsers/AnimationData.h` |
| 50 | `Include/Onyx/Domain/BoundingBox.h` |
| 50 | `Include/Onyx/Parsers/TextureData.h` |
| 49 | `Include/Onyx/Domain/MeshVertex.h` |
| 48 | `Include/Onyx/Parsers/MeshData.h` |
| 48 | `Include/Onyx/Types/TypeId.h` |
| 48 | `Include/Onyx/Parsers/SceneNode.h` |
| 47 | `Include/Onyx/Domain/MediaKind.h` |
| 46 | `Include/Onyx/Vfs/IFile.h` |
| 44 | `Include/Onyx/Types/TypeCatalog.h` |
| 37 | `Include/Onyx/Types/TypeRegistrar.h` |
| 35 | `Include/Onyx/Schema/DataTypes.h`, `Schema/AssetNode.h`, `Modules/GameModule.h`, `Services/Diagnostics.h`, `Services/Settings.h` |
| 34 | `Domain/ByteRange.h`, `Domain/Entry.h` |
| 33 | `Modules/Probe.h`, `Services/Jobs.h`, `Services/EventBus.h` |
| 32 | `Services/Logger.h` |
| 31 | `Modules/Workspace.h` |
| **24** | **`Include/Onyx/Fonts/SFSymbols.h`** |

Include-line frequency (`grep '#include <Onyx/' Source Tests Tools Examples`): `Services/Logger.h` 25, `Types/TypeCatalog.h` 18, `Modules/Workspace.h` 11, `Fonts/SFSymbols.h` 11 (direct), `Services/ThemeManager.h` 10, `Services/Diagnostics.h` 10, `Services/Appearance.h` 10, `Rendering/VkContext.h` 9, `Modules/DecoderRegistry.h` 9, `Domain/Entry.h` 9, `App/Widgets.h` 9.

**SFSymbols.h transitive inclusion: 24 TUs** (all TUs that include Types, Domain, Services-Diagnostics, Viewers, App, Fonts, etc.). Chain: `ITypeHandler.h` includes SFSymbols.h directly, and TypeCatalog/TypeRegistry/MediaKind/AssetVisibility (Core TUs) reach it. Full list of 24 TUs: `Examples/MinimalViewer/Source/Main.cpp`, `Source/Api/ToolkitApi.cpp`, `Source/App/{App,InfoTab,SettingsWindow,TitleBar,ViewerRegistry,Window}.cpp`, `Source/App/Panels/UiGallery.cpp`, `Source/App/Platform/{SystemFileDialog,Window_linux,Window_windows}.cpp`, `Source/Domain/MediaKind.cpp`, `Source/Fonts/IconTable.cpp`, `Source/Services/AssetVisibility.cpp`, `Source/Types/{TypeCatalog,TypeRegistry}.cpp`, `Source/Viewers/{ImageViewer,TextEditorViewer,VideoPlayer,Viewport3D}.cpp`, `Tests/Consumer/consumer_project/main.cpp`, `Tests/types_registry_test.cpp`, `Tools/ColdStart/ColdStartOnyx.cpp`.

Conclusion: yes, SFSymbols.h is pulled into many TUs transitively, including Core TUs, through `ITypeHandler.h`. The header is 6686 lines (384 KB). The root CMake already generates a table from it (`SFSymbolsTable.inl`) in the build tree, so the 6.6k-line header is parsed for the table and again by every TU in the 24-TU list above.

### 3.4 Other weight notes

- `imgui_lib` compiles `imgui_demo.cpp` into the library.
- `imgui_lib` compiles `imgui_impl_vulkan.cpp`, so every Onyx_Shell link carries Vulkan-backend symbols.
- glslang builds a full compiler toolchain at configure/build time for 8 small shaders.
- The Windows build downloads and links a full FFmpeg shared build (Media on by default).

---

## 4. CI

### 4.1 Local workflow (`.github/workflows/ci.yml`, the only workflow)

Triggers: `push`, `pull_request`, `workflow_dispatch`. No path filters, no concurrency group, no cache step.

| Job | Runner | Steps | Config | Skipped / notes |
|---|---|---|---|---|
| `windows-msvc` | `windows-latest` | checkout, `ilammy/msvc-dev-cmd@v1` (x64), `cmake --preset debug`, `cmake --build --preset debug`, **cold-start half 1** (`cl /c` of `Tools/ColdStart/ColdStartOnyx.cpp` with explicit `-I` dirs, shell cmd), `ctest --test-dir build -j1 --output-on-failure` | Debug, Ninja, Media ON (FFmpeg zip downloaded at configure) | render tests exit 77 (no GPU) and report "Not Run" |
| `linux-lavapipe` | `ubuntu-latest` | checkout, `apt-get install ninja-build mesa-vulkan-drivers libvulkan1 xorg-dev`, locate lavapipe ICD by glob, `cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Debug -DONYX_COMPONENT_MEDIA=OFF`, build, ctest | Debug, gcc, Media OFF | cold-start `cl /c` step not ported (documented in the workflow as follow-up) |

- No `actions/cache`, no ccache, no FetchContent cache. Every run re-fetches all deps and re-downloads the FFmpeg zip on Windows.
- No `ctest -R` filtering, so the full suite runs on both jobs.
- The Linux job disables Media (FFmpeg not pinned on Linux, as the workflow notes).

### 4.2 Stale claims in the repo about CI

- The header comment of `ci.yml` says the workflow "has NEVER actually executed on a GitHub-hosted runner" and that the remote "has never received the feat/onyx-v1-m4-vulkan branch".
- README Platform support table: Linux "a CI job is written (`linux-lavapipe`, software Vulkan) but **has never executed** — this history has never been pushed, so no run of that workflow exists."
- The `ci.yml` Test-step comment says 9 render-labelled tests have `SKIP_RETURN_CODE 77` among "53 registered tests". Current local count: **10** tests carry `SKIP_RETURN_CODE 77` (OnyxCliRender, OnyxCliRenderViews, OnyxCliRenderStrict, VkBootSmoke, VkValidationSelfTest, VkSceneSmoke, RenderToImageSmoke, VkAnimation, OracleReproducible, VkOracleParity). `ctest -N` in the local build dir lists **56** tests, not 53.

### 4.3 Remote run history (read-only `gh run list` / `gh run view`, 32 most recent runs on `JeanxPereira/OnyxSDK`, 2026-08-19 to 2026-10-16)

**Contradicts the "never executed" claim.** The `linux-lavapipe` job has executed in essentially every run:

| Outcome across the 32 runs (job-level) | Count |
|---|---|
| `linux-lavapipe` = failure | 31 (of 31 runs with job data; 1 run had no job data) |
| `windows-msvc` = success | 28 |
| `windows-msvc` = failure | 3 |

Latest linux-lavapipe failure detail (run 38937682802, branch `fix/macos-native-window-namespace`, 2026-10-16): **3 of 56 tests failed**:
- `VkOracleParity` (render-labelled) exit 1.
- `OnyxUnit`: `Tests/jobs_test.cpp:131: CHECK_FALSE( weak.expired() )`, 282 test cases, 1 failed.
- `OnyxJobs`: same assertion, 1 failed.

The README and CI header statements are therefore out of date. Whether the failures are a lavapipe tuning issue (VkOracleParity) or a real Linux-specific `weak_ptr` lifetime issue (Jobs) is not determined here.

Also on the remote: the last ~8 runs are on macOS-fix branches (`fix/macos-window-namespace`, `fix/macos-native-window-namespace`), and the run list shows failures there. `gh` output on the remote for those runs was not inspected beyond the job summary.

Remote tags: `v1.3.1`, `v1.3.2`, `v1.3.3` (not in local clone).

---

## 5. Tests

### 5.1 Inventory of `Tests/` (46 files outside Golden/, plus 10 golden fixtures)

Top-level `Tests/*.cpp`: 41 (40 `*_test.cpp` + `main.cpp`). `Tests/Consumer/consumer_project/main.cpp`: 1. Plus 3 CMakeLists, 1 `.cmake`.

| File | TEST_CASE | Exercises | SDK layer |
|---|---|---|---|
| `oracle_corpus_test.cpp` | 30 | oracle corpus scenes/textures | Tools/OnyxOracle (compiled into onyx_tests) |
| `cli_test.cpp` | 28 | `Onyx::Cli::Run` dispatch, probe/list/extract/decode/render | Core (Cli) + CliRender |
| `rendervk_test.cpp` | 19 | SPIR-V compile of 8 shaders, RenderContext, DeferredDestroyQueue | Render |
| `mounts_test.cpp` | 14 | MountSpec, Extract, OnyxBox obxpak | Core (Vfs/Workspace) + ExampleBox |
| `workspace_test.cpp` | 13 | Workspace open/close/async/cancel | Core (Modules) |
| `sessionlog_test.cpp` | 13 | SessionLog | Core (Services) |
| `appearance_test.cpp` | 13 | Appearance | Core/Services |
| `viewerrouting_test.cpp` | 10 | ViewerRouting/ViewerOpening | App (routing only) |
| `settings_test.cpp` | 10 | Settings | Core (Services) |
| `testkit_test.cpp` | 9 | TestKit goldens/decode/compare | TestKit |
| `gltf_test.cpp` | 9 | glTF export | Exchange |
| `logger_test.cpp` | 8 | Logger | Core (Services) |
| `jobs_test.cpp` | 7 | JobQueue, cancellation | Core (Services) |
| `eventbus_test.cpp` | 7 | EventBus | Core (Services) |
| `decoderregistry_test.cpp` | 7 | DecoderRegistry | Core (Modules) |
| `probe_test.cpp` | 6 | Probe ranking | Core (Modules) |
| `onyxbox_test.cpp` | 6 | OnyxBox end-to-end | ExampleBox |
| `framescheduler_test.cpp` | 6 | FrameScheduler | Core (Services) |
| `documentwindow_test.cpp` | 6 | DocumentWindow | Shell (App/Viewers) |
| `animclip_test.cpp` | 5 | animation clip | Render/Core |
| `theme_contrast_test.cpp` | 4 | ThemeManager contrast | Shell (Services) |
| `selection_test.cpp` | 4 | Selection | Core (Modules) |
| `recentfiles_test.cpp` | 4 | RecentFiles | Core (Services) |
| `pathutils_test.cpp` | 4 | PathUtils | Core (Services) |
| `metrics_test.cpp` | 4 | Metrics | Core (Services) |
| `materialdesc_test.cpp` | 4 | ResolveRoleIndices | Core (Parsers/Schema) |
| `infotabfilter_test.cpp` | 4 | InfoTabFilter | Shell (App) |
| `threading_test.cpp` | 3 | Threading | Core (Services) |
| `statusbarformat_test.cpp` | 3 | StatusBarFormat | Shell (App) |
| `diagnostics_test.cpp` | 3 | Diagnostics | Core (Services) |
| `assetvisibility_test.cpp` | 3 | AssetVisibility | Core (Services) |
| `appconfig_test.cpp` | 3 | AppConfig (toml) | Shell (Services) |
| `viewport3d_bounds_test.cpp` | 2 | Viewport3D::LoadScene bounds | Shell (Viewers) |
| `vfs_transformfile_test.cpp` | 2 | TransformFile, MemoryFile | Core (Vfs) |
| `types_registry_test.cpp` | 2 | TypeRegistry | Core (Types) |
| `typeregistrar_test.cpp` | 2 | TypeRegistrar | Core (Types) |
| `container_chunktree_test.cpp` | 2 | ChunkTree | Core (Container) |
| `container_chunkreader_test.cpp` | 2 | ChunkReader | Core (Container) |
| `sanity_test.cpp` | 1 | smoke | n/a |
| `container_chunkschema_test.cpp` | 1 | ChunkSchema | Core (Container) |
| `main.cpp` | 0 | doctest main | n/a |
| `Consumer/consumer_project/main.cpp` | — | `<Onyx/Onyx.h>` consumer stub | n/a (ExternalConsumption) |

Total `TEST_CASE(` occurrences in `Tests/`: 280 (the Linux CI log reports 282 test cases executed by `OnyxUnit`).

### 5.2 Golden fixtures

`Tests/Golden/corpus/`: 10 files, **437 KB** total. Largest: `sphere-grid-textured.png` 136 KB, `sphere-grid.png` 100 KB, `joint-chain-200.json` 72 KB, `skinned-cube.png` 60 KB, `blend-stack.png` 28 KB, `joint-chain-200.png` 24 KB, plus four small JSONs (4 KB and 1 KB). Five scenes, each a PNG and a JSON. Consumed only by `VkOracleParity` (four-tier tolerance compare, GPU-gated).

### 5.3 Consumer gate (`ExternalConsumption`)

`Tests/Consumer/CMakeLists.txt` adds one ctest entry (label `surface`, TIMEOUT 2400). `ExternalConsumptionTest.cmake` configures `consumer_project/` as its own top-level project in a fresh build dir. That project `add_subdirectory()`s the Onyx root from outside its source tree, with `ONYX_BUILD_EXAMPLES`, `ONYX_BUILD_TESTS`, `ONYX_COMPONENT_MEDIA` all OFF, and links `Onyx::Onyx`. `DEPS_CACHE` points at the outer build's `_deps` so no network refetch is needed. Purpose per its comments: catch `CMAKE_SOURCE_DIR` resolving to the consumer's root (the v1.1.0 README fix).

### 5.4 Render/GPU-gated tests

Ten tests carry `SKIP_RETURN_CODE 77`. On a GPU-less runner they report "Not Run" and do not fail. On the local dev box (AMD RX 6750 XT per CHANGELOG) they run. On the remote Linux lavapipe job they run and VkOracleParity fails (section 4.3).

### 5.5 Layers and files with zero direct test coverage

Method: grep of each `Source/*.cpp` basename and its `Include/` header against `Tests/*.cpp` includes and symbol names. Coverage here means a doctest reference, not a ctest that runs the code path (e.g. `RenderToImageSmoke` runs `onyx-oracle`, not a doctest).

**No doctest reference at all (symbol/header grep):**
- Audio: `Source/Audio/AdpcmDecoder.cpp`.
- Fonts: `Source/Fonts/FontManager.cpp`, `Source/Fonts/IconTable.cpp`.
- Vfs: `Source/Vfs/IsoFile.cpp`, `Source/Vfs/IsoFileSystem.cpp` (`OsFile` is tested indirectly through mounts/workspace).
- Schema: `Source/Schema/NodeInstance.cpp`, `Source/Schema/StructDef.cpp`.
- Services: `Source/Services/ScaleManager.cpp`, `Source/Services/TaskManager.cpp`.
- Rendering: `AxisGizmo.cpp`, `OffscreenTarget.cpp`, `Pipelines.cpp`, `RenderToImage.cpp` (ctest-only via onyx-oracle), `SceneRendererVk.cpp` and `VkContext.cpp` (referenced only by viewport3d/animclip/rendervk tests, plus ctest), `VkResources.cpp`, `VmaImpl.cpp`, `Camera.cpp` (viewport3d only).
- Api: `Source/Api/ToolkitApi.cpp`.
- Cli: `Source/Cli/Render.cpp` (ctest-only: OnyxCliRender*, GPU-gated).
- Viewers: `ImageViewer.cpp`, `TextEditorViewer.cpp`, `VideoPlayer.cpp`.
- App (all UI; zero doctest refs): `ActiveAnimation.cpp`, `AnimationTimeline.cpp`, `App.cpp`, `AssetNodeRenderer.cpp`, `FontDebuggerWindow.cpp`, `InfoTab.cpp`, `NativeMenuBar.cpp`, `SettingsWindow.cpp`, `StatusBar.cpp`, `TitleBar.cpp`, `ViewerRegistry.cpp`, `Widgets.cpp`, `WindowDecorator.cpp`, and 7 `Panels/*.cpp` (AnimCurveView, CameraPanel, DocumentBrowser, Dopesheet, InspectorPanel, UiGallery, WadStatsView).
- Platform: `Source/App/Platform/{NativeWindow,Window,SystemFileDialog}_{linux,windows}.cpp`, `Source/Platform/SystemTheme_{linux,windows}.cpp`.

Plus the CHANGELOG admits: "the swapchain frame path has zero automated coverage" and "the Shell wiring … has no automated gate at all". No test creates a real window.

Layers with test coverage: Core Container, Services, Types, Modules, Vfs (partial), Cli (partial, ctest), TestKit, Exchange, Render (shaders, RenderContext, DeferredDestroyQueue, plus GPU-gated oracle), a thin slice of Shell (ViewerRouting, DocumentWindow, Viewport3D bounds, StatusBarFormat, InfoTabFilter, ThemeManager contrast, AppConfig).

---

## 6. Tools

### 6.1 `Tools/ColdStart/`

- Files: `CMakeLists.txt` (35 lines), `ColdStartOnyx.cpp` (338 lines). No README.
- Purpose (from the file header): cold-start exam of `<Onyx/Onyx.h>`. Compile-only half is in CI (`cl /c` with explicit `-I`, no `-I Source`, no `-I build/generated`). Link-and-run half is the `ColdStartToolkit` EXE linking `Onyx::Onyx` only, plus the `ColdStart` ctest entry (label `surface`, no SKIP).
- The `--boot` path (window/Vulkan) is linked but never reached by the test.
- Built by default: yes (unconditional).

### 6.2 `Tools/OnyxOracle/`

- Files (size): `Main.cpp` 1925 lines (the largest file in the repo), `CorpusScenes.cpp` 582, `RenderReport.cpp` 216, `CorpusTextures.cpp` 122, `PngWrite.cpp` 25, `RenderToImageSmoke.cpp` 99, headers, `CMakeLists.txt` 176, `ReproTest.cmake`, `VkParityTest.cmake`.
- Purpose: headless Vulkan reference renderer. Commands: `--vk-smoke`, `--vk-validation-selftest`, `--vk-scene-smoke`, `--render-to-image-smoke`, `--vk-animation-smoke`, `render-corpus --renderer vk`, `compare` (four-knob tolerance), `verify` (byte-exact).
- Renders the five-scene corpus and compares to frozen goldens (`Tests/Golden/corpus`). Golden produced by the deleted GL renderer.
- Built by default: yes (unconditional).
- Its CorpusScenes/CorpusTextures/RenderReport sources are also compiled into `onyx_tests`.
- CMake comment names an AI model in a shipped file ("Adjudication (opus)", CHANGELOG-acknowledged).

### 6.3 Other tooling

- `Examples/OnyxCli` (`onyxbox-cli`): headless CLI, default-built with examples.
- `Examples/MinimalViewer`: GUI example with `--selftest`, default-built with examples.

---

## 7. Docs

### 7.1 `docs/` contents

| Path | Bytes | One line |
|---|---|---|
| `docs/design/2026-08-18-onyx-v1-architecture.md` | 26363 | v1 architecture document (layers, target split W1, Core/Render/Shell) |
| `docs/design/2026-08-19-public-surface-audit.md` | 26135 | the public-surface audit: can a third party build a toolkit on Onyx |
| `docs/design/appearance-system.md` | 11152 | appearance/theme system design |
| `docs/gltf-validation.md` | 6634 | glTF export validation in Blender, status "PENDING A HUMAN" |
| `docs/superpowers/` | 17 files | historical specs and plans (see 7.2) |

### 7.2 `docs/superpowers/` (17 files, dated by filename)

Plans (12):
- 2026-06-15 `onyx-config-redesign-phase-a` — Onyx Config Redesign Phase A — Implementation Plan
- 2026-06-15 `onyx-panel-composition` — Onyx Panel Composition + App Chrome — Implementation Plan
- 2026-06-15 `onyx-profile-driven-open` — Onyx Profile-Driven Open Dialog — Implementation Plan
- 2026-08-18 `onyx-v1-m0-oracle-corpus` — Onyx v1 M0 — Oracle Corpus
- 2026-08-18 `onyx-v1-m1-target-split` — Onyx v1 — M1: Target Split
- 2026-08-18 `onyx-v1-m2-identity-state` — Onyx v1 — M2: Identity & State
- 2026-08-18 `onyx-v1-m3a-module-contracts` — Onyx v1 — M3a: Module Contracts, Workspace and Headless CLI
- 2026-08-18 `onyx-v1-m3b-shell-rewiring` — Onyx v1 — M3b: Shell on the Workspace, Legacy Retired
- 2026-08-18 `onyx-v1-roadmap` — Onyx v1 Roadmap — Milestones
- 2026-08-19 `onyx-v11-vulkan-animation` — Onyx v1.1.0 — Vulkan Skeletal Animation
- 2026-08-19 `onyx-v1-m4-vulkan` — Onyx v1 M4 — Vulkan Renderer
- 2026-08-19 `onyx-v1-m5-generality` — Onyx v1 M5 — Generality

Specs (5):
- 2026-06-15 `onyx-config-redesign-phase-a-design` — Config Redesign — Phase A (window chrome + layout) Design
- 2026-06-15 `onyx-panel-composition-design` — Panel Composition + App Chrome — Design
- 2026-06-15 `onyx-profile-driven-open-design` — Profile-Driven Open Dialog — Design
- 2026-06-16 `onyx-category-tree-design` — Category Tree View + SCUMM Resource Tree — Design
- 2026-08-19 `onyx-v11-vulkan-animation-design` — Skeletal animation on Vulkan, plus the two trivial gaps

The 2026-06 items (config redesign, panel composition, profile-driven open, category tree with a SCUMM resource tree) show the pre-v1 product was a game-asset explorer with per-game format hooks. The v1 series (M0 to M5) is the milestone sequence toward generality, and its plans reference a `.superpowers/sdd/` ledger that does not exist in this repo (see section 9).

### 7.3 CHANGELOG.md

1361 lines, versions: v1.2.0 (2026-08-23), v1.1.0 (2026-08-19), v1.0.0 (2026-08-19), v0.6.0 (2026-08-18), v0.5.3, v0.5.2, v0.5.1, v0.5.0, v0.2.0, v0.1.0.

**Known gaps, v1.2.0 (verbatim-ish):**
- "Coverage is a cutout, not a blend. A soft mask still resolves to a hard edge at the cutoff, because ordered transparency needs depth-sorted draw submission that `SceneRendererVk` does not do yet."
- "`Height`, `Detail` and `Emissive` remain resolved by `ResolveRoleIndices` and sampled by nothing."

**Known gaps, v1.1.0 (the v1.1 promise):**
- No outline/hover-highlight pass, no wireframe or matcap shading. `RenderBatch::isHighlighted` unread. Wireframe and Outline colors are dead knobs disabled in Settings.
- **`IDocumentContent::DrawInspector()` has no caller anywhere in the Shell.** Stranded items include `VideoPlayer`'s stream-metadata inspector and `Viewport3D`'s per-batch visibility checkboxes. Per-batch culling is real in the renderer and unreachable from the GUI.
- The mapped joint-palette and other mapped buffers in `SceneRendererVk.cpp` assume `HOST_COHERENT`, which VMA does not guarantee for `CPU_TO_GPU`, and nothing calls `vmaFlushAllocation`. Pre-existing since M4.
- Carried forward: no `install()`/`export()` (audit G4); `Services/PathUtils.h` declares `namespace PathUtils` at global scope (G5, open half); parity detection floor roughly 0.41 to 0.68 percentage points of high-delta pixels.
- "The animation UI's layout choices have had no human eye-pass" (strip heights sized by reasoning, not observation). The Shell wiring has no automated gate.

**Known gaps, v1.0.0 (the v1.0 promise), headline items:**
- No animation playback in `SceneRendererVk` (since reverted/added in v1.1.0; `AnimationPlayer` exists but nothing constructs one at v1.0).
- No per-batch visibility culling; no outline/wireframe/matcap.
- Four viewport color pickers are dead knobs (Bones/Wireframe/Outline).
- Parity gate detection floor: `sphere-grid-textured` 0.3941% (0.406 pt headroom), `sphere-grid` 0.3922%, `joint-chain-200` 0.2220%, `skinned-cube` 0.1549%, `blend-stack` 0.1225%. A defect confined to one small silhouette (~0.13% of frame) is not caught.
- `Viewport3D` redraws via blocking `OneShot` submit on the UI thread (45 FPS observed with vsync). `RenderContext::AddPass` is the intended home for a non-blocking version. Not started.
- `RenderRequest` grew background fields per consumer (grow-one-field-per-consumer pattern).
- `Tools/OnyxOracle` calls `Build()` a second time per scene, only to recover batches for the JSON report.
- `<Onyx/Render.h>` has a hard ImGui dependency through `AxisGizmo.h` (ImDrawList). Candidate fix: move AxisGizmo into Shell.
- macOS unsupported: `Window_macos.mm`/`NativeWindow_macos.mm` still call `NSOpenGLContext` on a `GLFW_NO_API` window; `VkContext` lacks `VK_KHR_portability_enumeration` and `VK_EXT_metal_surface`. Nobody has macOS hardware to test.
- Closing a "Decoding..." placeholder tab does not cancel the decode job.
- All decoding is serialized on one lane (`kDecodeLane`).
- CLI `render` collapses GPU-init, pipeline, readback, and PNG-write failures onto `kUsage`.
- Swapchain frame path has zero automated coverage; all "0 validation messages" claims are manual GUI runs.
- `Services/PathUtils.h` declares `namespace PathUtils` at global scope and includes `<windows.h>` unconditionally on Windows (leak; still open).
- Five public headers use `#include "imgui.h"` (quoted).
- `Include/Onyx/Cli/Gltf.h` declares `MakeGltfExportFn`, which ships in no library (the G1 issue again, in one place).
- `Tools/OnyxOracle/CMakeLists.txt` names an AI model in a shipped build file.
- `Examples/**` still spells `Onyx_ExampleBox` (no `Onyx::` alias).

### 7.4 Other doc-ish places

- `README.md` (18793 bytes): sections Features, Platform support, Quick Start, Consuming Onyx, CLI Usage, UI test mode, Architecture, Dependencies, Stability policy, Conventions, Brand assets, License.
- `.github/Logo.png` (116 KB) is in git.

---

## 8. Git churn

### 8.1 Commit history (local clone)

- 269 commits total on the local branch history.
- Last 10 subjects (local): `6dd2be6 feat(app): a native folder picker…`, `c6fca56 feat(app): let a host drop engine menu entries…`, `23899d0 feat(viewers): let a host hide the "Viewer" window…`, `95a7c43 feat(app): let a host answer File > Open…`, `dddce38 Merge PR #2 feat/opacity-role`, `5a932fd feat(render): Sample a material's opacity map`, `410991d Merge branch 'feat/onyx-v11-vulkan-animation' — v1.1.0`, `5d5aeb9 fix(modules): Reach ContainerContext::fileTable from every module`, `74f0c73 fix(modules): Give ContainerContext the path it withholds from parsers`, `3861911 fix(build): Resolve Onyx's own paths through the repo root`.
- Full `git log --format='%h %s' | head -80` was captured; the most recent 80 span 2026-08-19 to 2026-09-19.

### 8.2 Fix/bug/regression/revert

Counted across all 269 local subjects (case-insensitive substring):
- Subjects matching `fix|bug|regression|revert`: **72** (about 27% of commits).
- Of these, 68 begin with `fix`.
- `revert`: 0. `regression`: 0. `bug`: 0 as a literal word in subjects.

### 8.3 Hotspot files (`git log --name-only`, top 25, all history including renamed/deleted paths)

| Commits | Path | Exists in HEAD? |
|---|---|---|
| 58 | `CMakeLists.txt` | yes |
| 52 | `Tests/CMakeLists.txt` | yes |
| 37 | `CHANGELOG.md` | yes |
| 22 | `Tools/OnyxOracle/Main.cpp` | yes |
| 20 | `Tools/OnyxOracle/CMakeLists.txt` | yes |
| 20 | `Source/App/Window.cpp` | yes |
| 14 | `Source/Viewers/Viewport3D.cpp` | yes |
| 12 | `Source/Rendering/SceneRenderer.cpp` | **no** (renamed/deleted: GL renderer) |
| 12 | `Source/Cli/Commands.cpp` | yes |
| 12 | `Source/App.cpp` | **no** (moved under `Source/App/`) |
| 11 | `Source/Modules/Workspace.cpp` | yes |
| 11 | `docs/superpowers/plans/2026-08-18-onyx-v1-roadmap.md` | yes |
| 10 | `Tests/cli_test.cpp` | yes |
| 10 | `Source/App/App.cpp` | yes |
| 10 | `README.md` | yes |
| 9 | `Tests/oracle_corpus_test.cpp` | yes |
| 9 | `Source/Rendering/AnimationPlayer.cpp` | yes |
| 9 | `Include/Onyx/Viewers/Viewport3D.h` | yes |
| 9 | `Include/Onyx/Services/AppConfig.h` | yes |
| 9 | `Include/Onyx/Onyx.h` | yes |
| 9 | `Include/Onyx/Modules/Workspace.h` | yes |
| 9 | `Include/Onyx/App/Window.h` | yes |
| 8 | `Tests/mounts_test.cpp` | yes |
| 8 | `Source/Viewers/DocumentWindow.cpp` | yes |
| 8 | `Source/Ui/Viewers/Viewport3D.cpp` | **no** (historical path) |

Read: the root and test-root CMake files are the most churned files (CMake changed 58 and 52 times). The Shell/Window/Viewport3D/App cluster is the main UI churn. `Tools/OnyxOracle/Main.cpp` and the 1925-line file are the biggest single-file churn. Note that `CMakeLists.txt` and `Tests/CMakeLists.txt` are the two most edited files, consistent with the "every fix adds a ctest entry" pattern.

---

## 9. Housekeeping

### 9.1 `dist/`

- Contents: `Glyph.svg` (logo glyph), `Logo.svg`, `Logo.afdesign` (Affinity Designer source). Total 32 KB on disk.
- Tracked in git (`git ls-files dist` lists all three). `.gitattributes` marks `*.afdesign` as binary.
- Purpose: brand assets. Not referenced by any CMake file or code. Keeping the `.afdesign` master in git is reasonable for a brand source file; the `.svg` exports are fine. Nothing here is a build product.

### 9.2 `.gitignore` (8 lines + blank)

Entries: `build/`, `Build/`, `build-*/`, `.vs/`, `.vscode/`, `.idea/`, `*.user`, `*.suo`, `cmake-build-*/`, `out/`.

Sanity:
- `build/` and `Build/` are redundant on Windows (case-insensitive FS). `git check-ignore` reports the match against `Build/`.
- `build-release/` matches `build-*/`, which is good (CMakePresets release binaryDir).
- `build/` in the working tree is **1.3 GB** (ignored, not in git). Local `build/_deps/` contains doctest, glm, glslang and vulkan_headers source trees and their own tests, which is why `grep add_test` over the repo returns extra entries unless `build/` is excluded.
- Missing: `.superpowers/` (referenced in comments, see section 9.5), no `*.log`, no `*.pdb`, no `compile_commands.json`. Not a defect, just unlisted.

### 9.3 Large tracked files (`git ls-files`, du -k, top 15)

| KB | Path |
|---|---|
| 4108 | `third_party/miniaudio/miniaudio.h` |
| 1760 | `third_party/fonts/SFSymbols.ttf` |
| 384 | `Include/Onyx/Fonts/SFSymbols.h` |
| 280 | `third_party/stb/stb_image.h` |
| 136 | `Tests/Golden/corpus/sphere-grid-textured.png` |
| 116 | `.github/Logo.png` |
| 100 | `Tools/OnyxOracle/Main.cpp` |
| 100 | `Tests/Golden/corpus/sphere-grid.png` |
| 92 | `third_party/fonts/SFMono-Regular.otf` |
| 88 | `CHANGELOG.md` |
| 72 | `third_party/stb/stb_image_write.h` |
| 72 | `Tests/Golden/corpus/joint-chain-200.json` |
| 60 | `Tests/Golden/corpus/skinned-cube.png` |
| 56 | `Source/Viewers/Viewport3D.cpp` |
| 56 | `Source/Rendering/SceneRendererVk.cpp` |

Total tracked: 370 files, about 10.5 MB (`git ls-files` du). `.gitattributes` sets `*.png`, `*.jpg`, `*.zip`, `*.otf`, `*.ttf`, `*.afdesign` to binary.

### 9.4 `third_party/`

`fonts/` (1.9 MB: SFSymbols.ttf, SFMono-Regular.otf), `miniaudio/` (4.1 MB: miniaudio.c and miniaudio.h), `stb/` (352 KB: stb_image.h, stb_image_write.h). Nothing else. The README says that nothing else is vendored. The `build/_deps` tree under the repo root is not vendored, it is FetchContent output.

### 9.5 Comments referencing files that do not exist in the repo

- `.superpowers/sdd/2026-08-19-onyx-v1-m4-vulkan/` and `.superpowers/sdd/2026-08-19-onyx-v1-m5-generality/` (ledgers). Referenced by CHANGELOG ("gone at merge") and by many CMake comments (`task-7-report.md`, `task-1-report.md`). `.superpowers/` does not exist in the working tree.

### 9.6 Other housekeeping

- `.clang-format` and `.editorconfig` present. `.gitattributes`: `* text=auto eol=lf`.
- `LICENSE` present.
- Git remote `origin` = `https://github.com/JeanxPereira/OnyxSDK.git`.
- Local branches include `backup/pre-split`, `feat/appearance-system`, `fix/selection-dangling-on-close`, `fix/typeregistry-lazy-index`, `local/pre-update-4632c81`, `wip/session-log-2026-08-31`, `main`, `feat/host-ui-hooks` (current).
- Remote branch `origin/fix/typeregistry-lazy-index` and `origin/fix/selection-dangling-on-close` exist.

---

## 10. Version

`Include/Onyx/Version.h`: **working-tree diff is line endings only**. Content is identical to HEAD modulo CR characters (`git diff --ignore-cr-at-eol` is empty for the value lines).

- HEAD: LF endings, `ONYX_VERSION_STRING "1.2.0"`, `MAJOR 1 MINOR 2 PATCH 0`.
- Working tree: same values, **CRLF** line terminators. Git warns "CRLF will be replaced by LF the next time Git touches it".
- Source template `cmake/Version.h.in`: LF.
- Root `project(OnyxSDK VERSION 1.2.0 ...)`: matches.
- Last commit touching the file: `5a932fd` (2026-08-23).

Interpretation: `configure_file()` in the root CMake writes this tracked file on every configure. The current working copy was written with CRLF (likely by a Windows configure or an editor), so git sees a whole-file diff without content change. This is a maintainability trap: the generated header is in the source tree and under version control, and every configure can dirty it.

---

## 11. Surprises and risks (ranked)

1. **The "Linux CI has never executed" claim is false on the remote.** About 31 linux-lavapipe runs exist, all failing at the Test step, with 3 real test failures in the latest (OnyxUnit/OnyxJobs `weak.expired()` assertion, VkOracleParity). README and `ci.yml` header are stale.
2. **Remote is ahead of local.** Tags v1.3.1–v1.3.3 and runs through 2026-10-16 exist only on the remote. Any survey of "current" state from the local clone is at v1.2.0.
3. **SFSymbols.h (6686 lines) reaches Core TUs** through `ITypeHandler.h`, and 24 TUs transitively include it. The table is generated from it at build time, so it is parsed twice.
4. **Every consumer downloads and builds all of glfw, imgui, implot, glslang (with Python), Vulkan headers, volk, VMA, and the Windows FFmpeg zip**, even if it only links `Onyx::Core`. Only FFmpeg and doctest are gated.
5. **No PCH and no unity build**, and a header closure of up to 82 Onyx headers per TU.
6. **Test-skip count drift:** CI comments say 9 render tests skip and 53 registered; current tree has 10 skip-marked and 56 registered.
7. **Coverage hole:** Shell/App UI (about 30 files), Platform windowing, Fonts, Vfs ISO, Schema NodeInstance/StructDef, Audio ADPCM, Viewers ImageViewer/TextEditor/VideoPlayer, ScaleManager, TaskManager, Api/ToolkitApi have no doctest references.
8. **`Tools/OnyxOracle` (1925-line Main.cpp) and both ColdStart and onyx-oracle are built even with `ONYX_BUILD_TESTS=OFF`**, and `Tools/OnyxOracle` sources are compiled into `onyx_tests` directly.
9. **`Onyx_Media` and `Onyx_Shell` both compile `Source/App/TexturePool.cpp`**, by design (CMake comment), to avoid a link cycle. Duplicate compile, duplicate object code.
10. **`Onyx_Core` has a Windows-only include (`<windows.h>` in SessionLog.cpp) and a private toml++ dependency (Settings.cpp).** LayerGuard does not catch these because it only forbids imgui/implot/GLFW/glad.
11. **`CMakeLists.txt` and `Tests/CMakeLists.txt` are the most churned files** (58 and 52 commits), consistent with per-fix ctest entries.
12. **Working-tree `Include/Onyx/Version.h` is rewritten by configure with CRLF**, so git sees a whole-file change with no content change.
13. **glslang 14.3.0 and FFmpeg Windows zip are the two heaviest downloads/builds**, and the FFmpeg zip has no hash.
14. **The Linux FFmpeg path is unpinned** (pkg-config) and never built in CI (Media off).

---

## Appendix: scratch scripts

- `C:\Users\usuario\AppData\Local\Temp\claude\D--CodingProjects-OnyxSDK\894ddaf1-d60a-4b62-a684-58156d2d3c69\scratchpad\incl_graph.py`: transitive include graph, SFSymbols reach, header fan-in.
- `C:\Users\usuario\AppData\Local\Temp\claude\D--CodingProjects-OnyxSDK\894ddaf1-d60a-4b62-a684-58156d2d3c69\scratchpad\core_external.py`: Core TU UI/GPU/platform reach check.
- `C:\Users\usuario\AppData\Local\Temp\claude\D--CodingProjects-OnyxSDK\894ddaf1-d60a-4b62-a684-58156d2d3c69\scratchpad\runids.txt`: remote run IDs.

No files in the repo were modified.
