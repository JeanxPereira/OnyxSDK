# Onyx v2 — W4 Media + Cli + Exchange + Prism Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver `Onyx::Exchange` (glTF 2.0 with animation, PNG, WAV, registered as `decode --to` exporters), the `render` verb in `Onyx::Render`, the CLI's explicit `--first` policy with golden outputs, the Ui/Shell gaps the wave needs (viewers over the `Any` slot, file hand-off for video entries, audio pause/seek, the Gallery panel, the Shell's audio output hook), `Onyx::Media` (`MediaSource` over FFmpeg with one decode thread, `AudioOutput` over miniaudio, `MediaView`), the second synthetic module **Prism** with its `PrismUi` palette viewer and settings schema, and the `OnyxCli` example — with `MinimalViewer` carrying both modules and Media.

**Architecture:** Edges only point down the §4 list (Core, Gpu, Render, Ui, Shell, Media, Cli, Exchange, TestKit). `Render` and `Exchange` contribute to the CLI through the `Core::Cli` contract (`VerbTable`, `Exporters`) and never link `Onyx::Cli`; the `render` verb writes its PNG through whatever `png` exporter the table holds. `Shell` cannot link `Media`, so a host wires Media in two lines (`Shell::SetAudioOutput`, `Media::RegisterMediaViewer`). `Media` touches FFmpeg only on its decode thread; the UI thread sees a mailbox of RGBA frames and a status snapshot. Prism is the generality exam: it is built with **zero edits under `Source/`**; a gap it hits is an SDK task, never a workaround. Every one of its types exercises a different decoder slot, including `Any`, `Table` and `Schema`.

**Tech Stack:** C++20; FFmpeg 7.1 (`libavformat`, `libavcodec`, `libavutil`, `libswscale`, `libswresample`; Windows: the BtbN `win64-gpl-shared` 7.1 archive by FetchContent, Linux: pkg-config); miniaudio (vendored in `third_party/miniaudio`); cgltf `v1.15` (header-only, FetchContent); stb (vendored); the existing Core/Gpu/Render/Ui/Shell layers; doctest.

**Spec:** `docs/superpowers/specs/2026-10-30-onyx-v2-design.md` (§4 layers and per-component fetch, §5.5 decoder slots, §8.1–8.3 Ui contracts, panels and the Gallery, §9 Shell, §10 Media, §11 Cli, §12 Exchange, §13 Prism, §14 examples, §16 W4 gate). v1 reference (read-only, never included): `legacy/Source/Exchange/GltfExport.cpp` (clean; port it), `legacy/Tests/gltf_test.cpp`, `legacy/Source/Viewers/VideoPlayer.cpp` (the behaviour; its three-thread design and its seek race and packet leaks are **not** ported — survey bugs #4, #5), `legacy/Source/App/Panels/UiGallery.cpp` (the pages), `legacy/CMakeLists.txt` lines 160–260 (FFmpeg and cgltf discovery).

## Global Constraints

- Branch `v2`. Commit per task with Conventional Commits, imperative Sentence-case subject **of at most 72 characters**, explicit paths in `git add`, **no attribution line of any kind**. Never push.
- The verdict is `py -3 tools/check.py --preset windows-debug --jobs 4` (use `--jobs 2` in a lane while other lanes build). Use `--ctest-regex` while iterating. Never run `check.py` in a tree where another implementer is building.
- Public headers only under `Source/<Layer>/include/Onyx/<Layer>/`; every public header compiles standalone; namespaces `Onyx::Media`, `Onyx::Exchange`, `Onyx::Render`, `Onyx::Cli`, `Onyx::Ui`, `Onyx::Shell`; Prism in `Prism::`. Lint forbids: in Media `GLFW/`; in Exchange, Cli, Core, TestKit everything graphical (imgui, volk, VMA, vulkan, GLFW, windows.h, libav, miniaudio); in Ui `GLFW/`, `libav`, `miniaudio`; in Shell `libav`. No public header of Exchange includes cgltf or stb; no public header of Media includes a `libav*` or `miniaudio.h` header (opaque `std::unique_ptr<Impl>`): a consumer of `Onyx::Media` compiles without FFmpeg's headers on its include path.
- No singletons; the only process-wide object stays `Log::Default()`. FFmpeg's global state (`avformat_network_init` is not called; no `av_log_set_callback` leaking across hosts) is left alone.
- No game name (`gow`, `gowr`, `gow1`, `gow2`, `ragnarok`, `wad`, `vag`, `vpk`, `pss`) under `Source/`, `Examples/`, `Tests/`; no console name; no comment narrates a task, milestone, fix round, review or previous version. English only. PascalCase files and types.
- Error codes introduced here: `exchange.gltf.empty`, `exchange.gltf.write`, `exchange.png.format`, `exchange.png.write`, `exchange.wav.write`, `render.noPng`, `render.noScene`, `cli.ambiguous`, `media.open`, `media.stream`, `media.seek`, `media.audio`, `ui.open.any`.
- Suites: `Exchange.Unit`, `Media.Unit` (needs FFmpeg at build time, no device, no display), `Media.Gpu` (SKIP 77 without a device), `Prism.Unit`, `Examples.Cli`; existing suites gain cases. GPU suites keep `ValidationMessageCount() == 0` at the end of every case through `Tests/Support/ValidationLayer.h`.
- Prism's implementer may not edit anything under `Source/`, `cmake/`, `tools/` or another example. A missing contract is reported as `NEEDS_CONTEXT` naming the gap; the controller turns it into an SDK task.

## Review Focus

1. A video entry that lives inside a container at a byte offset, not in a file of its own: `MediaSource` reads through `Vfs::IFile` at that range and never asks FFmpeg for a path (Task 6).
2. `decode mesh.prm --to gltf` on a 200-joint skinned scene with a clip: the file parses and validates under cgltf, the skin lists every joint, the animation has one sampler per channel kind per joint (Task 2).
3. `decode box.obx img` when two entries are named `img`: without `--first` the verb exits `kUsage` and lists the candidate `@paths`; with `--first` it takes the earliest (Task 4).
4. A `.prm` opened with and without `PrismUi` installed: without it the palette entry opens in the stock table viewer; with it the palette viewer wins by `TypeKey` (Task 5, Task 8).
5. Closing a `MediaView` tab mid-playback and closing the window with a clip playing: the decode thread and the miniaudio device stop within one second and no frame is uploaded after the view is gone (Task 6, Task 8).

---

## Dependency map and lanes

```
Wave A:            T1 deps (FFmpeg, miniaudio, cgltf), layer CMake, test rigs, CI, tool docs      [main tree]
Wave B (parallel): T2 Exchange [lane] | T3 Render `render` verb [lane] | T4 Cli --first + goldens [lane] | T5 Ui/Shell gaps [lane]
Wave C (parallel): T6 Media [lane, from the tree after T5 merged] | T7 Prism module + OnyxCli [lane, from the tree after T4 merged]
Wave D:            T8 PrismUi, MinimalViewer, Shell/Examples tests                                [main tree]
Wave E:            T9 docs, spec drift, README, CLAUDE.md                                         [main tree]
```

Sources and tests are globbed; lanes never edit the same CMake file. T1 is the only task that edits `cmake/`, `Tests/CMakeLists.txt`, `Source/*/CMakeLists.txt`, `.github/workflows/ci.yml`. T7 adds `Examples/Prism/CMakeLists.txt`, `Examples/OnyxCli/CMakeLists.txt`, `Tests/Prism/CMakeLists.txt` and edits `Examples/CMakeLists.txt`; T8 edits `Examples/MinimalViewer/CMakeLists.txt` and `Tests/Examples/CMakeLists.txt`.

**Rulings taken while writing this plan** (the spec is the authority; these settle what it leaves open):

- §4 says `cli.Add(Exchange::CliVerbs())`; Exchange ships no verb of its own, only exporters, so its entry point is `Exchange::AddExporters(Cli::VerbTable&)`. §11's `--to gltf|png|wav` is what it provides.
- §14 says MinimalViewer stays under twenty lines; with Prism, PrismUi and Media wired it is about twenty-five. The spec is updated in T9 to say so; the alternative (a fourth example just for Media) adds an example nobody asked for.
- §13 says TestKit "adds fixture builders for a synthetic ISO and a synthetic animated scene": both exist since W2 (`IsoBuilder`, `BuildAnimatedChain`). Module files are built by the modules' own writers (`OnyxBox::Writer`, `Prism::Writer`), because TestKit may not depend on an example. No TestKit task in W4.
- A `.mp4` on disk has no module and does not open in the workspace; video is an **entry** of `MediaKind::Video` inside a module's container (the v1 case). Prism stays palette-centric and FFmpeg-free, so the Media tests build their own clip with FFmpeg's encoders inside a padded file (offset ≠ 0) and open it through `MediaSource` directly and through a stub module's entry.
- Hover-delay polling in `ShellFrame` (parked from the W3 re-review) lands in T5.

---

## Task 1: Dependencies, layer CMake, test rigs, CI

**Files:**
- Create: `cmake/Deps/Ffmpeg.cmake`, `cmake/Deps/Miniaudio.cmake`, `cmake/Deps/Cgltf.cmake`
- Modify: `cmake/Assets.cmake` (add `onyx_install_media_runtime`), `cmake/Deps/Stb.cmake` (no change expected; verify `Onyx_stb` is reusable), `Source/Media/CMakeLists.txt`, `Source/Exchange/CMakeLists.txt`, `Source/Render/CMakeLists.txt` (no new dep; verify), `Tests/CMakeLists.txt` (`add_subdirectory(Exchange)`, `add_subdirectory(Media)` under `ONYX_WITH_MEDIA`), `tools/lint.py` (no table change; verify Media's row), `tools/README.md`, `.github/workflows/ci.yml`, `README.md` consuming section (one sentence: Media needs FFmpeg; Windows downloads it, Linux needs the dev packages)
- Create: `Source/Media/Pch.h`, `Source/Exchange/Pch.h`, `Tests/Exchange/CMakeLists.txt`, `Tests/Exchange/Main.cpp`, `Tests/Exchange/Layer.cpp`, `Tests/Media/CMakeLists.txt`, `Tests/Media/Main.cpp`, `Tests/Media/Layer.cpp`, `Tests/Media/Gpu/Main.cpp` (probe main returning 77 without a device, as `Tests/Ui/Gpu/Main.cpp`)

**Interfaces:**
- Produces `onyx_dep_ffmpeg()` → INTERFACE target `ffmpeg::ffmpeg`. Windows: `FetchContent` of `https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-03-31-13-11/ffmpeg-n7.1.3-43-g5a1f107b4c-win64-gpl-shared-7.1.zip` (URL in the cache variable `ONYX_FFMPEG_WIN_URL`), include dir `<root>/include`, link `avformat avcodec avutil swscale swresample` `.lib`, and the cache variable `ONYX_FFMPEG_DLLS` (the `bin/*.dll` list). Elsewhere: `find_package(PkgConfig REQUIRED)` + `pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale libswresample)` and link `PkgConfig::FFMPEG`. A missing FFmpeg is a configure error whose message names `ONYX_WITH_MEDIA=OFF` and the apt packages; no silent downgrade.
- Produces `onyx_dep_miniaudio()` → STATIC `Onyx_miniaudio` from `third_party/miniaudio/miniaudio.c` with `MA_NO_ENCODING MA_NO_DECODING MA_NO_GENERATION MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH` as PRIVATE definitions (playback only), warnings off for that target, alias `miniaudio::miniaudio`, PUBLIC include of `third_party/miniaudio`.
- Produces `onyx_dep_cgltf()` → FetchContent `jkuhlmann/cgltf` tag `v1.15`, `SOURCE_SUBDIR _skip_add_subdirectory`, INTERFACE `Onyx_cgltf` with the source dir as PUBLIC include, alias `cgltf::cgltf`.
- Produces `onyx_install_media_runtime(<target>)`: on Windows, a post-build `copy_if_different` of every `ONYX_FFMPEG_DLLS` next to the target; a no-op elsewhere. Called by every executable that links `Onyx::Media` (the Media test executables here; MinimalViewer in T8).
- Produces `Source/Media/CMakeLists.txt`: `onyx_dep_ffmpeg()`, `onyx_dep_miniaudio()`, `onyx_add_layer(Media PUBLIC_DEPS Onyx::Ui DEPS ffmpeg::ffmpeg miniaudio::miniaudio)` — PRIVATE, because no public Media header includes them. `Source/Exchange/CMakeLists.txt`: `onyx_dep_cgltf()`, `onyx_dep_stb()`, `onyx_add_layer(Exchange PUBLIC_DEPS Onyx::Core DEPS cgltf::cgltf Onyx_stb)`.
- Produces `Tests/Exchange/CMakeLists.txt`: `onyx_add_tests(Exchange LINK Onyx::TestKit Onyx::Cli cgltf::cgltf)` (the tests parse the output with cgltf and drive `decode --to` through the stock verbs). `Tests/Media/CMakeLists.txt`: `onyx_add_tests(Media LINK Onyx::TestKit Onyx::ExampleBox ffmpeg::ffmpeg)` and `onyx_add_tests(Media SUITE Gpu SKIP_RETURN_CODE 77 LINK Onyx::TestKit Onyx::ExampleBox ffmpeg::ffmpeg)`, both followed by `onyx_install_media_runtime(OnyxMediaTests)` / `(OnyxMediaGpuTests)`. The Media tests link FFmpeg because the fixture encoder (T6) calls libavcodec directly.
- Produces in `ci.yml` (Linux apt line): `libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev pkg-config`; a comment that `Media.Unit` runs on both runners and `Media.Gpu` skips on Windows. `tools/README.md`: the FFmpeg requirement, the WSL note (`-DONYX_WITH_MEDIA=OFF` when the dev packages cannot be installed).

- [ ] **Step 1: Write the three dep files, the runtime helper, the two layer CMakeLists and the test CMakeLists** with a rig case per new suite (`TEST_CASE("Exchange layer name")` → `CHECK(Onyx::Exchange::LayerName() == "Exchange")`; the same for Media; `Media.Gpu` main probes a device and returns 77 without one).
- [ ] **Step 2: Verify** `py -3 tools/check.py --preset windows-debug --jobs 4 --ctest-regex "Exchange|Media|Consumer"`: `_deps` gains `ffmpeg_prebuilt` and `cgltf`; the FFmpeg DLLs sit next to `OnyxMediaTests.exe`; `Consumer.CoreOnly` still finds no `ffmpeg` directory in its `_deps`. Then configure once with `-DONYX_WITH_MEDIA=OFF` into a scratch build dir (`cmake --preset windows-debug -B build/scratch-nomedia -DONYX_WITH_MEDIA=OFF` inside `vcvars64`; `check.py` has no flag for this, so do it by hand and delete the dir) and confirm no FFmpeg fetch happens.
- [ ] **Step 3: Commit** `build: Add FFmpeg, miniaudio and cgltf deps with the Media and Exchange rigs`

## Task 2: Exchange — glTF with animation, PNG, WAV, exporters

**Files:**
- Create: `Source/Exchange/include/Onyx/Exchange/{Gltf,Png,Wav,Exporters}.h`, `Source/Exchange/{Gltf,Png,Wav,Exporters}.cpp`
- Test: `Tests/Exchange/{Gltf,Png,Wav,Exporters}.cpp`

**Interfaces:**
- Consumes: `Domain::Scene`, `Domain::Texture`, `Domain::Audio`, `Domain::AnimClip` (`Key<T>{time, value}` per joint channel), `Cli::VerbTable::Exporters()` + `Exporters::Add(format, kind, ExportFn)` where `ExportFn = Result<void>(const void* payload, const std::filesystem::path&)` and the payload is `Domain::Scene*` for `Mesh`, `Domain::Texture*` for `Image`, `Domain::Audio*` for `Audio`; `TestKit::BuildAnimatedChain()`, `BuildJointChain200()`, `BuildSphereGrid()` (textured), `TestKit::ReadPng`.
- Produces (`Gltf.h`):
  ```cpp
  struct GltfOptions { bool embedBuffers = true; bool includeSkin = true; bool includeAnimations = true; };
  // .glb when embedBuffers, else .gltf + sibling <stem>.bin. One mesh, one primitive per MeshPart (POSITION, NORMAL when present,
  // TANGENT when present, TEXCOORD_0/1 when present, COLOR_0 when present, JOINTS_0/WEIGHTS_0 when the scene has a skeleton and
  // the part's jointMap is not empty, indices as u32), one material per Domain::Material (baseColorFactor, metallicFactor,
  // Diffuse → baseColorTexture, Normal → normalTexture, Occlusion → occlusionTexture; other roles are not exported), textures
  // re-encoded as PNG inside the buffer (only Rgba8 textures; a block-compressed texture is skipped with the material slot left
  // unset), one node per Joint with its localBind decomposed to TRS, inverseBindMatrices from Joint::inverseBind, and one glTF
  // animation per non-additive AnimClip: per channel one sampler + channel per populated key list (rotation, translation,
  // scale), LINEAR, input times as given. Additive clips are skipped. Every accessor carries min and max.
  // Errors: exchange.gltf.empty (no parts), exchange.gltf.write (cgltf or the filesystem refused).
  Result<void> WriteGltf(const Domain::Scene& scene, const std::filesystem::path& out, const GltfOptions& options = {});
  ```
- Produces (`Png.h`): `Result<void> WritePng(const Domain::Texture& texture, const std::filesystem::path& out);` — Rgba8 only (`exchange.png.format` otherwise), `exchange.png.write` on failure; the implementation TU defines `STB_IMAGE_WRITE_STATIC` and `STB_IMAGE_WRITE_IMPLEMENTATION` so its symbols never clash with TestKit's.
- Produces (`Wav.h`): `Result<void> WriteWav(const Domain::Audio& audio, const std::filesystem::path& out);` — RIFF/WAVE, PCM 16-bit little-endian, `channels` interleaved at `sampleRate`, floats clamped to [-1, 1] and rounded; `exchange.wav.write` on failure.
- Produces (`Exporters.h`): `void AddExporters(Cli::VerbTable& table);` registering `gltf` (Mesh → `WriteGltf` with defaults), `png` (Image → `WritePng`), `wav` (Audio → `WriteWav`).

- [ ] **Step 1: Write the failing tests.** `Gltf.cpp`: "WriteGltf round-trips the animated chain through cgltf" (`cgltf_parse_file` + `cgltf_load_buffers` + `cgltf_validate` return `cgltf_result_success`; `meshes_count == 1`; `skins_count == 1` with `joints_count == skeleton->joints.size()`; `animations_count == 1`; `animations[0].channels_count` equals the number of non-empty key lists over all channels; the inverse bind accessor count equals the joint count; every accessor has `has_min && has_max`); "WriteGltf exports all 200 joints of the joint chain"; "includeSkin=false omits the skin"; "includeAnimations=false omits the animation"; "embedBuffers=false writes a .gltf and a sibling .bin"; "WriteGltf refuses a scene with no parts" (`exchange.gltf.empty`); "the sphere grid's diffuse texture lands as an embedded PNG image" (`images_count == 1`, `textures_count == 1`, the material's `pbr_metallic_roughness.base_color_texture.texture` is set); "a block-compressed texture is skipped" (a scene whose texture is `Bc1` exports with `images_count == 0` and ok). `Png.cpp`: "WritePng round-trips through TestKit::ReadPng" (`MakeChecker(8, ...)`, bytes equal) and "WritePng refuses a Bc1 texture" (`exchange.png.format`). `Wav.cpp`: "WriteWav writes a 44-byte header and the samples" (two channels, four frames, hand-decoded header fields `RIFF`, `WAVE`, `fmt ` chunk size 16, format 1, channels 2, rate 8000, bits 16, data size 16; the sample `-1.0f` → `0x8000`, `1.0f` → `0x7FFF`). `Exporters.cpp`: "AddExporters registers gltf, png and wav with their kinds" and "decode --to png writes the file" through `Cli::Run` with `StockVerbs()` plus `AddExporters` over `Tests/Cli/Support/StubModule.h`'s image entry (the stub is header-only over Core; include it as `../Cli/Support/StubModule.h`).
- [ ] **Step 2: Run** `py -3 tools/check.py --preset windows-debug --jobs 2 --ctest-regex Exchange` → FAIL.
- [ ] **Step 3: Implement** the four files. Port `legacy/Source/Exchange/GltfExport.cpp`'s cgltf construction (one buffer, aligned views, PNG images inline) onto `Domain::Scene`; drop every v1 `Parsers`/`GpuVertex`/`ObjectData` reference; the rest-pose TRS comes from `Joint::localBind` (`glm::decompose` or a hand decomposition that leaves `scale` at 1 when the matrix is orthonormal).
- [ ] **Step 4: Run** → PASS. **Verify** `py -3 tools/lint.py` clean (no cgltf/stb in a public header).
- [ ] **Step 5: Commit** `feat(exchange): Write glTF with animation, PNG and WAV as decode exporters`

## Task 3: Render — the `render` verb

**Files:**
- Create: `Source/Render/include/Onyx/Render/CliVerbs.h`, `Source/Render/CliVerbs.cpp`
- Test: `Tests/Render/CliVerbs.cpp` (argument errors, no device), `Tests/Render/Gpu/CliVerbs.cpp` (the render, SKIP 77)

**Interfaces:**
- Consumes: `Cli::Verb`/`VerbTable`/`Exporters` (Core), `Cli::Run` exit codes `kOk = 0, kUsage = 1, kNoModule = 2, kStrictErrors = 3, kPartial = 4` (Core: `Onyx/Core/Cli/Run.h`), `Modules::Workspace::Open`, `Modules::Resolve`/`FindByName`, `DecoderRegistry::DecodeScene`, `Gpu::Device::Create`, `Render::RenderToImage(device, scene, view, overlays, extent)`, `Render::View`, `Projection`, `TestKit::ImageCompare`, `Tests/Render/Gpu/Support/RenderFixture.h`.
- Produces: `std::vector<Cli::Verb> CliVerbs();` holding `render`: usage `<file> <entry|@path> --out <png> [--width N] [--height N] [--clip N --time S] [--grid] [--first] [--game <hint>]`. Defaults 512×512, the camera framing `scene.bounds` from the +Z side (the same framing `SceneView` uses on open; copy the formula, do not link Ui). `--clip/--time` set `View::pose`. The PNG is written through `table.Exporters().Find("png")`; when none is registered the verb prints `render.noPng: no png exporter registered` and returns `kUsage`. A scene that does not decode prints `render.noScene` and returns `kPartial`. The device is created with `Gpu::DeviceOptions{}` (no validation); `gpu.noDevice` prints the code and returns `kPartial`. Name resolution follows T4's policy: duplicates without `--first` → `cli.ambiguous`, `kUsage` (implement the same `ResolveEntry` rule here; T4 owns the Cli copy and the two stay in sync by the shared test wording).

- [ ] **Step 1: Write the failing tests.** `Tests/Render/CliVerbs.cpp`: "render without --out exits kUsage and prints the usage"; "render without a png exporter exits kUsage with render.noPng" (a table holding only `CliVerbs()`, a stub module from `../Cli/Support/StubModule.h`, a scene entry). `Tests/Render/Gpu/CliVerbs.cpp`: "render writes a PNG of the blend stack" (a table with `CliVerbs()` plus a `png` exporter implemented in the test over `TestKit::WritePng`; `--width 64 --height 64`; the file exists; `TestKit::ReadPng` gives 64×64; the image is not uniform) and "render --clip 0 --time 0.5 poses the animated chain" (two renders differ).
- [ ] **Step 2: Run** `--ctest-regex Render` → FAIL. **Implement.**
- [ ] **Step 3: Run** → PASS, zero validation messages. **Commit** `feat(render): Add the render verb writing through the png exporter`

## Task 4: Cli — explicit `--first`, failures in exit codes, CLI goldens

**Files:**
- Modify: `Source/Cli/Common.h`, `Source/Cli/Common.cpp` (shared `ResolveEntry`), `Source/Cli/Decode.cpp`, `Source/Cli/Extract.cpp`, `Source/Cli/List.cpp` (only if it resolves names), `Source/Cli/Probe.cpp` (no change expected)
- Create: `Tests/Cli/Goldens/{probe-box,list-box,list-box-json,decode-box-image,decode-box-ambiguous}.txt`, `Tests/Cli/Goldens.cpp`
- Test: `Tests/Cli/Verbs.cpp` (extend), `Tests/Cli/OnyxBoxVerbs.cpp` (extend)

**Interfaces:**
- Consumes: `Detail::Args` (`positional, json, strict, game, to, out`), `ParseArgs`, `Modules::FindByName`, `Modules::Resolve`, `TestKit::CompareTreeGolden` (a byte-exact text compare; reused for CLI output), `UpdateGolden` (`ONYX_UPDATE_GOLDENS=1`).
- Produces: `Args` gains `bool first = false` (`--first`). `const Domain::AssetEntry* ResolveEntry(const Modules::Document&, std::string_view target, bool first, std::ostream& err)` in `Common.h`: `@path` resolves directly; a name with one match resolves; a name with several matches and `first` takes the earliest in depth-first order; several matches without `first` writes `cli.ambiguous: N entries are named 'x': @0/1, @3, ...` to `err` and returns null, and the verb returns `kUsage`. `decode` and `extract` use it (`extract <file> [<entry>...]` already reports failures through `kPartial`; verify, do not rewrite). Usage strings mention `--first`. Output stays byte-stable: no timestamps, no absolute paths (the goldens use a fixture written to a temp dir and replace the path with `<file>` before comparing).

- [ ] **Step 1: Write the failing tests.** `Verbs.cpp`: "decode with a duplicated name and no --first exits kUsage naming the paths" (stub script with two `img` entries; `err` contains `cli.ambiguous` and `@1`); "decode --first takes the earliest match"; "extract --first". `Goldens.cpp`: one case per golden file running the verb over `SampleBox()` (from `OnyxBoxVerbs.cpp`; move `SampleBox` into `Tests/Cli/Support/BoxFixture.h` so both files share it), replacing the temp path with `<file>`, comparing with `CompareTreeGolden(actual, ONYX_SOURCE_DIR "/Tests/Cli/Goldens/<name>.txt", diff)` (`onyx_add_tests` defines `ONYX_SOURCE_DIR`).
- [ ] **Step 2: Run** `--ctest-regex Cli` → FAIL. **Implement**; generate the goldens once with `ONYX_UPDATE_GOLDENS=1`, read every file and check its content is what the spec promises (probe: module id and confidence; list: the tree with kinds and sizes; decode: `image img 8x8 rgba8`).
- [ ] **Step 3: Run** → PASS. **Commit** `feat(cli): Require --first on ambiguous names and add CLI goldens`

## Task 5: Ui and Shell gaps — Any-slot viewers, file hand-off, audio pause/seek, Gallery, audio hook, hover polling

**Files:**
- Modify: `Source/Ui/include/Onyx/Ui/ViewerRegistry.h` (`OpenRequest`, `ViewerSpec`), `Source/Ui/Viewers/Opening.cpp`, `Source/Ui/ViewerRegistry.cpp`, `Source/Ui/include/Onyx/Ui/AudioOutput.h`, `Source/Ui/include/Onyx/Ui/Viewers/AudioView.h`, `Source/Ui/Viewers/AudioView.cpp`, `Source/Shell/include/Onyx/Shell/Shell.h`, `Source/Shell/Shell.cpp`, `Source/Shell/ShellFrame.cpp`, `Examples/Embedded/Main.cpp` (only if its `OpenRequest` literal breaks; it should not — fields are appended)
- Create: `Source/Ui/include/Onyx/Ui/Panels/Gallery.h`, `Source/Ui/Panels/Gallery.cpp`
- Test: `Tests/Ui/ViewerOpening.cpp` (extend), `Tests/Ui/ViewerRegistry.cpp` (extend), `Tests/Ui/AudioView.cpp` (extend; its fake output gains the new methods), `Tests/Ui/Gallery.cpp`, `Tests/Shell/Window/Shell.cpp` (extend), `Tests/Shell/Window/ShellFrame.cpp` (extend)

**Interfaces:**
- Produces (`ViewerRegistry.h`): `OpenRequest` gains, appended in this order, `std::shared_ptr<const Vfs::IFile> file; Domain::ByteRange range; std::shared_ptr<void> any; std::string anyCapability;` — `file`/`range` are filled for **every** request (the entry's source file and byte range) so a viewer of a bare kind (Raw, Video) streams without a decode; `any` is filled when the routed spec names a capability. `ViewerSpec` gains `std::string anyCapability;` (empty = none). `OpenEntry`: a spec with `anyCapability` decodes `DecodeAny(ctx, capability)` on the job lane (a null result reports `ui.open.any` on the document and never places the view); `NeedsDecode(Video)` stays false. `ViewerRegistry::Route`: a `TypeKey` spec with `anyCapability` applies only when `Capabilities::any` lists that capability.
- Produces (`AudioOutput.h`): `IAudioOutput` gains `virtual void Pause() = 0; virtual void Resume() = 0; virtual void Seek(double seconds) = 0; virtual bool Paused() const = 0;` (`Playing()` stays true while paused; `Position()` holds during a pause). `AudioView` gains a Pause/Resume button, a click on the waveform seeks (`Seek(fraction * Duration())`), and `double Position() const` for tests.
- Produces (`Gallery.h`): `class Gallery final : public IPanel` — id `onyx.gallery`, title `Gallery`, `visible = false` by default, **not** registered by the Shell (a host adds it). Pages as tabs: Widgets (every `Widgets::*` function in its states, plus ImGui's basic controls), Theme (the resolved palette as swatches with the token name and a contrast ratio against the window background; rows under 4.5 marked), Typography (the three fonts of `UiContext::fonts` at the base size, or a hint when `fonts` is null), Icons (the `IconTable()` grid with a filter box and a cached match list rebuilt only when the filter text changes, click copies the define name to the clipboard), Style (read-only dump of the applied `ImGuiStyle` scalars). Nothing it changes is persisted; it never writes `ImGuiStyle`.
- Produces (`Shell.h`): `void SetAudioOutput(std::unique_ptr<Ui::IAudioOutput>)`; `Context()` sets `UiContext::audio` to it; `Teardown` calls `Stop()` on it before the device goes.
- Produces (`ShellFrame.cpp`): the frame also polls (no idle wait) while `ImGui::GetCurrentContext()->HoverItemDelayId != 0` and the hover timer is below `style.HoverDelayNormal`, so delayed tooltips appear on time with the mouse still.

- [ ] **Step 1: Write the failing tests.** `ViewerRegistry.cpp`: "a TypeKey spec with anyCapability routes only when the type reports it". `ViewerOpening.cpp`: "OpenEntry hands a bare entry its file and range" (the hex entry's request has `file` non-null and `range == entry.source`); "OpenEntry decodes the Any slot for a spec naming its capability" (a stub-registered `any` decoder returning a `shared_ptr<int>`; the created view receives it through the request; a capability the type lacks never places and reports `ui.open.any`). `AudioView.cpp`: "Pause keeps the position and Resume continues"; "a click on the waveform seeks" (drive through the fake output's recorded calls). `Gallery.cpp`: "Gallery draws every page without asserts and emits geometry" (headless rig, `visible = true`, click each tab through `ImGui::TabItemButton`? no — call the page draw through a `DrawPage(index)` test hook; each page's window `DrawList->CmdBuffer.Size > 0`); "the icon filter caches its matches" (same filter twice → the match vector's data pointer unchanged; a new filter → different count). `Shell/Window/Shell.cpp`: "SetAudioOutput reaches the context" (`Context().audio` is the object; after `Teardown` through `Run`'s close path the fake saw `Stop`). `ShellFrame.cpp`: "a pending hover delay keeps the frame polling" (set `HoverItemDelayId` through a hovered `IconButton` with `DelayShort`; the next 3 Steps take well under the idle cap).
- [ ] **Step 2: Run** `--ctest-regex "Ui|Shell"` → FAIL. **Implement.**
- [ ] **Step 3: Run** → PASS; zero validation messages in the window cases. **Commit** in three: `feat(ui): Route Any-slot viewers, hand entries their file, pause audio`, `feat(ui): Add the Gallery panel`, `feat(shell): Add the audio output hook and poll through hover delays`.

## Task 6: Media — MediaSource, AudioOutput, MediaView

**Files:**
- Create: `Source/Media/include/Onyx/Media/{MediaSource,AudioOutput,MediaView,Viewers}.h`, `Source/Media/{MediaSource,MediaSourceImpl.h,AvioFile.cpp,AvioFile.h,AudioOutput,MediaView,Viewers}.cpp`
- Test: `Tests/Media/Support/ClipFixture.h`, `Tests/Media/Support/ClipFixture.cpp`? (header-only is simpler: a static inline encoder), `Tests/Media/{MediaSource,AudioOutput,MediaView}.cpp`, `Tests/Media/Gpu/MediaView.cpp`

**Interfaces:**
- Consumes: `Vfs::IFile` (`Size`, `ReadAt`), `Ui::IAudioOutput` (T5 shape), `Ui::IDocumentView`, `UiContext` (`textures` nullable, `audio`), `TexturePool::Create/Update/Remove/Size`, `OpenRequest::file/range` (T5), `ViewerRegistry::Register`, `Widgets::IconButton`, `Icons`.
- Produces (`MediaSource.h`, no FFmpeg type in the header):
  ```cpp
  struct MediaInfo { double duration = 0; uint32_t width = 0, height = 0; double fps = 0; double displayAspect = 0;
                     std::string videoCodec, audioCodec; bool hasVideo = false, hasAudio = false; };
  struct VideoFrame { double pts = 0; uint32_t width = 0, height = 0; std::vector<std::byte> rgba; };
  enum class PlayState : uint8_t { Stopped, Playing, Paused, Ended };
  class MediaSource {  // one decode thread; every FFmpeg object lives on it; the UI thread only reads snapshots
  public:
      MediaSource(); ~MediaSource();   // the destructor joins the thread and closes everything
      Result<MediaInfo> Open(std::shared_ptr<const Vfs::IFile> file, uint64_t offset, uint64_t size);  // media.open
      void Play(); void Pause(); void Stop(); void Seek(double seconds);   // commands queued to the decode thread
      PlayState State() const; double Position() const; const MediaInfo& Info() const;
      std::optional<VideoFrame> TakeFrame();   // the newest frame due at Position(); null when nothing new
      void SetVolume(float volume); float Volume() const;
  };
  ```
  Reads go through a custom `AVIOContext` over `IFile::ReadAt` bounded to `[offset, offset+size)` (`AvioFile`), seekable. The clock is the audio clock when the clip has audio (frames consumed by the miniaudio callback), a steady clock otherwise. `Seek` runs on the decode thread: `avformat_seek_file` then `avcodec_flush_buffers` on both codecs, then the frame mailbox and the audio ring are cleared — never from the UI thread. Packets and frames are RAII (`unique_ptr` with `av_packet_free`/`av_frame_free` deleters). The video mailbox holds the latest decoded frame not yet taken plus at most 4 ahead; the thread blocks on the mailbox when full, so a paused view costs nothing. Ended → `State() == Ended`, `Play` restarts from zero.
- Produces (`AudioOutput.h`): `class AudioOutput final : public Ui::IAudioOutput` over one miniaudio playback device (float32, the clip's rate and channels; the device is re-initialised when they change); `media.audio` when the device cannot start; `Position()` from frames the callback consumed; `Seek` moves the read cursor; thread-safe. No FFmpeg involved.
- Produces (`MediaView.h`): `class MediaView final : public Ui::IDocumentView` built from `(doc, node, name, file, range)`; id `onyx.media.<doc>.<name>`; opens the source on the first `Draw`, shows a transport bar (play/pause, stop, a seek slider over `Info().duration`, volume, the codec names and `fps`), uploads each taken frame through `ui.textures` (`Create` once, `Update` after; aspect from `displayAspect`), and draws the status text alone when `textures` is null. Destruction stops the source first (joins) and removes the texture. `MediaKind::Video` only.
- Produces (`Viewers.h`): `void RegisterMediaViewer(Ui::ViewerRegistry&)` registering `onyx.media` for `MediaKind::Video` (priority 0) creating a `MediaView` from the request's `file`/`range`.
- Produces (`Tests/Media/Support/ClipFixture.h`): `struct ClipFixture { std::filesystem::path path; uint64_t offset; uint64_t size; }` and `ClipFixture WriteClip(const std::string& name, bool withAudio, uint32_t frames = 50)`: encodes `mpeg1video` 64×48 at 25 fps, frame `i` a solid colour `(i * 5, 255 - i * 5, 128)`, muxed to AVI with `pcm_s16le` 44100 Hz stereo of a 440 Hz tone when `withAudio`, written **after 4096 bytes of zero padding** into a temp file (so `offset = 4096`); `Expected(i)` returns the colour. Plus `static std::array<uint8_t, 3> CentreColour(const VideoFrame&)`.

- [ ] **Step 1: Write the failing tests.** `MediaSource.cpp`: "Open reads a clip at an offset and reports its info" (`duration ≈ 2.0 ± 0.1`, `64×48`, `fps ≈ 25`, `hasAudio` per fixture, `videoCodec == "mpeg1video"`); "TakeFrame after Play yields frames in order with the expected colours" (first taken frame's centre within ±12 of `Expected(0)`; a later frame's pts > the first's); "Seek lands on the right frame" (`Seek(1.0)` → the next taken frame's colour within ±12 of `Expected(25)` and pts ≈ 1.0 ± 0.08); "Pause freezes Position and Resume continues"; "Stop then Play restarts from zero"; "Ten Open/destroy cycles each return within one second" (a `MediaSource` opened, played and destroyed in a loop; every destructor measured under 1 s); "Open fails with media.open on garbage" (a file of random bytes); "the video-only clip keeps time on the steady clock" (`withAudio = false`; Position advances ≈ wall time ± 0.1 over 0.5 s). `AudioOutput.cpp`: "Play a 0.2 s tone, Position advances and Playing drops at the end" (skip with `MESSAGE` when `media.audio` — the runner may have no audio device; the assertions after a successful start still run); "Pause holds Position, Seek moves it". `MediaView.cpp` (headless, no GPU): "MediaView draws the transport without a texture pool" (`textures == nullptr`; window has geometry; no crash across 10 frames while playing); "destroying a playing MediaView stops within one second". `Tests/Media/Gpu/MediaView.cpp`: "MediaView uploads the first frame into the pool" (build the device, `UploadContext`, `ImGuiVulkanBackend` binder and `TexturePool` the way `Tests/Ui/Gpu/TexturePool.cpp` does, with the `HeadlessImGui` rig from `Tests/Support`; `pool.Count() == 1` after a few frames; `Size(id) == {64, 48}`; zero validation messages).
- [ ] **Step 2: Run** `--ctest-regex Media` → FAIL. **Implement.** Keep `MediaSource.cpp` under ~400 lines by moving the AVIO adapter and the audio ring into their own files.
- [ ] **Step 3: Run** → PASS; `py -3 tools/lint.py` clean (no libav/miniaudio include in `Source/Media/include`). **Commit** `feat(media): Add MediaSource, AudioOutput and MediaView over FFmpeg and miniaudio`

## Task 7: Prism module, writer, tests, CLI goldens, OnyxCli example

**Files:**
- Create: `Examples/Prism/CMakeLists.txt`, `Examples/Prism/include/Prism/{Format,Module,Writer,Palette}.h`, `Examples/Prism/{Module,Writer}.cpp`, `Tests/Prism/CMakeLists.txt`, `Tests/Prism/Main.cpp`, `Tests/Prism/Support/PrismFixture.h`, `Tests/Prism/{Module,Decoders,Smoke,Golden}.cpp`, `Tests/Golden/prm/sample.golden.json` (tree golden, beside `Tests/Golden/obx/`), `Tests/Cli/Goldens/{probe-prism,list-prism,decode-prism-palette,decode-prism-stats}.txt`, `Tests/Cli/PrismVerbs.cpp`, `Examples/OnyxCli/CMakeLists.txt`, `Examples/OnyxCli/Main.cpp`
- Modify: `Examples/CMakeLists.txt` (`add_subdirectory(Prism)` beside OnyxBox; `add_subdirectory(OnyxCli)`), root `CMakeLists.txt` line adding `Examples/Prism` under `ONYX_BUILD_TESTS OR ONYX_BUILD_EXAMPLES` exactly like `Examples/OnyxBox`, `Tests/CMakeLists.txt` (`add_subdirectory(Prism)`), `Tests/Cli/CMakeLists.txt` (link `Onyx::ExamplePrism`)
- **Not allowed:** any file under `Source/`, `cmake/`, `tools/`, `Examples/OnyxBox`, `Examples/MinimalViewer`, `Examples/Embedded`.

**Interfaces:**
- Consumes: `IGameModule` (`Info, Probe, RegisterTypes, RegisterDecoders, RegisterSettings, Mounts, ParseContainer`), `TypeRegistrar::Add(TypeSpec{key, label, media, icon, color})`, `DecoderRegistry::{Image, Text, Audio, Table, Any, Schema}`, `Schema::Schema` (fields `U32`, `F32`), `ContainerContext` (`reader, path, settings, diags, progress, moduleState, roots, files`), `DecodeContext` (`reader, settings, moduleState`), `SettingsSchema::{Bool, Double}`, `Services::Settings::Get*` (whatever `OnyxBox::Module::RegisterSettings`/decoders use — copy the pattern), `TestKit::{DecodeAll, SnapshotTree, CompareTreeGolden, UpdateGolden}`, `Cli::StockVerbs`, `Render::CliVerbs`, `Exchange::AddExporters`, `Cli::Run`.
- Produces (`Format.h`, binary, little-endian; this layout is the plan's call per spec §17):
  ```
  PRM1 container
    "PRM1" | u16 paletteCount | u32 sectionCount | sectionCount x section header
    section header: u8 kind | u16 nameLen | name (UTF-8) | u32 offset (absolute) | u32 size
    kind 0 palette  paletteCount x RGBA8. Exactly one per file; parsed at ParseContainer time into moduleState
                    (shared_ptr<Prism::Palette>) because the indexed images need it to decode.
    kind 1 image    u16 width | u16 height | width*height bytes of palette indices
    kind 2 manifest UTF-8 text (language "toml")
    kind 3 clip     u32 sampleRate | u16 channels | u32 frameCount | frameCount*channels x i16 PCM
    kind 4 entries  u16 cols | u16 rows | rows x cols cells, each u16 len | UTF-8 bytes; columns named by the first row
    kind 5 stats    16 bytes: u32 imageCount | u32 clipCount | f32 gainDefault | u32 flags
  Defensive walk as OnyxBox: a section whose range leaves the file, an image whose size is not 4 + w*h, a clip whose size does
  not match frameCount*channels*2, an index past paletteCount (reported once per image at decode) → Failed + Error diagnostic;
  a header that runs past the end stops the walk and keeps what was read. No palette section → every image Failed with
  "prism.noPalette".
  ```
- Produces (`Palette.h`): `struct Palette { std::vector<glm::u8vec4> colors; };`
- Produces (`Module.h`): `class Module final : public IGameModule` — id `prm`, display name `Prism`, hints `{"prism"}`, open filter `{"Prism files", {"prm"}}`, probe: magic → 95 else 0. Types (key → MediaKind, slots): `prism.palette` → `Table` (Table decoder: columns `index, r, g, b, a`; **and** `Any` decoder with capability `prism.palette` → `shared_ptr<Palette>`); `prism.image` → `Image` (RGBA8 through the palette; setting `prism.transparentIndexZero` (Bool, default false) makes index 0 fully transparent); `prism.manifest` → `Text` (`language = "toml"`); `prism.clip` → `Audio` (samples × setting `prism.gain` (Double, default 1.0, range 0–4)); `prism.entries` → `Table`; `prism.stats` → `Raw` with `Schema` (`Schema("PrismStats")` with four fields `imageCount U32, clipCount U32, gainDefault F32, flags U32`). `RegisterSettings` declares the two keys. Icons through `Domain::DefaultIcon(kind)`; colours distinct per type.
- Produces (`Writer.h`): `class Writer` with `SetPalette(std::vector<glm::u8vec4>)`, `AddImage(name, w, h, std::vector<uint8_t> indices)`, `AddManifest(name, std::string_view)`, `AddClip(name, sampleRate, channels, std::vector<int16_t>)`, `AddEntries(name, std::vector<std::vector<std::string>>)`, `AddStats(name, imageCount, clipCount, gainDefault, flags)`, `AddRange(name, kind, offset, size)` (out-of-bounds records), `std::vector<std::byte> Build() const`.
- Produces (`Tests/Prism/Support/PrismFixture.h`): `std::vector<std::byte> SamplePrism()` — a 4-colour palette (red, green, blue, transparent-black), a 4×4 image whose indices spell the rows `0 1 2 3`, a manifest `title = "sample"`, a 0.1 s mono 8000 Hz clip (a ramp), a 3×2 entries table, a stats record `{1, 1, 1.0f, 0}`, and one out-of-range image `bad`.
- Produces (`Examples/OnyxCli/Main.cpp`, ≤ 30 lines): a `Workspace` with `OnyxBox::Module` and `Prism::Module`, a `VerbTable` with `Cli::StockVerbs()`, `Render::CliVerbs()`, `Exchange::AddExporters(table)`, `return Cli::Run(ws, table, argc, argv, std::cout, std::cerr)`. `Examples/OnyxCli/CMakeLists.txt` links `Onyx::Cli Onyx::Render Onyx::Exchange Onyx::ExampleBox Onyx::ExamplePrism` and is added under `ONYX_WITH_GPU` (Render); `Examples.Cli` in T8 is gated the same way. `Tests/Prism/CMakeLists.txt` mirrors `onyx_add_tests` by hand: `add_executable(OnyxPrismTests ...)` from `Tests/Prism/*.cpp`, links `Onyx::ExamplePrism Onyx::TestKit doctest::doctest`, `ONYX_GOLDEN_DIR`/`ONYX_SOURCE_DIR` definitions, `add_test(NAME Prism.Unit ...)`.

- [ ] **Step 1: Write the failing tests.** `Module.cpp`: "Probe accepts the magic and refuses other bytes"; "ParseContainer lists every section in order with its kind and type key"; "an out-of-range section is Failed with a diagnostic and the rest survives"; "a file without a palette flags every image". `Decoders.cpp`: "the palette decodes as a table and as the Any payload"; "the image decodes through the palette and honours prism.transparentIndexZero"; "the clip decodes with prism.gain applied"; "the manifest is toml text"; "the entries table keeps its columns"; "the stats schema reads the four fields" (`ReadSchema` + `ToJson` contains `"imageCount": 1`). `Smoke.cpp`: "DecodeAll decodes every slot with only bad in the allowlist" (`failed == 0`). `Golden.cpp`: tree golden of the sample. `Tests/Cli/PrismVerbs.cpp`: the four Prism goldens (probe, list, `decode sample.prm palette` → `table palette columns=5 rows=4` **and** `any palette: prism.palette`, `decode sample.prm stats` → the schema JSON line).
- [ ] **Step 2: Run** `--ctest-regex "Prism|Cli"` → FAIL. **Implement.** If any step needs something `Source/` does not offer, stop and report `NEEDS_CONTEXT` naming the contract.
- [ ] **Step 3: Run** → PASS; `OnyxCli probe <sample>` by hand prints `prm 95`. **Commit** `feat(examples): Add the Prism module with its writer, tests and OnyxCli` (and a second commit for the CLI goldens if the first subject would exceed 72 characters).

## Task 8: PrismUi, MinimalViewer with both modules and Media, window and example tests

**Files:**
- Create: `Examples/Prism/Ui/CMakeLists.txt`, `Examples/Prism/Ui/include/PrismUi/{PaletteView,Extension}.h`, `Examples/Prism/Ui/{PaletteView,Extension}.cpp`
- Modify: `Examples/Prism/CMakeLists.txt` (`add_subdirectory(Ui)` under `ONYX_WITH_SHELL`), `Examples/MinimalViewer/Main.cpp`, `Examples/MinimalViewer/CMakeLists.txt`, `Tests/Examples/CMakeLists.txt` (`Examples.Cli`: `OnyxCli` with no arguments must exit 1 and print a usage naming `probe`, `render` — a `CheckUsage.cmake` beside `CheckHelp.cmake`), `Tests/Shell/CMakeLists.txt` (link `Onyx::ExamplePrism Onyx::ExamplePrismUi` and, under `ONYX_WITH_MEDIA`, `Onyx::Media`), `Tests/Shell/Window/Shell.cpp` (extend)
- **Allowed under `Source/`:** nothing. A gap found here is reported, not patched.

**Interfaces:**
- Consumes: `IShellExtension::Install(Shell&)`, `Shell::{Viewers, Panels, Workspace, SetAudioOutput, Open, Step, Context}`, `ViewerSpec{id, target = TypeKey{"prism.palette"}, priority, create, anyCapability = "prism.palette"}`, `OpenRequest::any`, `Widgets`, `Media::{AudioOutput, RegisterMediaViewer}`, `Tests/Shell/Window/Support/WindowFixture.h`, `Tests/Prism/Support/PrismFixture.h`.
- Produces (`PaletteView.h`): `class PaletteView final : public Ui::IDocumentView` over a `std::shared_ptr<const Prism::Palette>`; id `prism.palette.<doc>.<name>`; draws one swatch per colour (a filled rect with the index and `#rrggbbaa` underneath; hover shows the RGBA), swatch size from `Widgets::MetricsFor(theme).rowHeight * 2`; `size_t Count() const`.
- Produces (`Extension.h`): `class Extension final : public Onyx::Shell::IShellExtension` whose `Install` registers `prism.palette` (viewer id `prism.palette`, priority 10, `anyCapability = "prism.palette"`) and nothing else (the settings schema is the module's). Target `PrismUi` (alias `Onyx::ExamplePrismUi`) links `Onyx::Shell Onyx::ExamplePrism`.
- Produces (`MinimalViewer/Main.cpp`): the Shell with `OnyxBox::Module`, `Prism::Module`, `Prism::Ui::Extension`, and under `#if ONYX_WITH_MEDIA` (a compile definition the CMakeLists sets when the option is on) `shell.SetAudioOutput(std::make_unique<Onyx::Media::AudioOutput>())` and `Onyx::Media::RegisterMediaViewer(shell.Viewers())`. About twenty-five lines. CMake links `Onyx::ExamplePrism Onyx::ExamplePrismUi` and `Onyx::Media` under the option, and calls `onyx_install_media_runtime(MinimalViewer)`.

- [ ] **Step 1: Write the failing tests.** `Tests/Shell/Window/Shell.cpp`: "a Prism file opens its palette in the palette viewer when the extension is installed" (`hiddenWindow`, `AddExtension(Prism::Ui::Extension)`, `Open(sample.prm)`, `Step` until ready, `OpenEntry` on the palette node through the documents panel's `Add`, `Step` ×5 → the active tab's `Id()` starts with `prism.palette.`); "without the extension the palette opens in the stock table viewer" (`onyx.table.`); under `ONYX_WITH_MEDIA`: "SetAudioOutput with Media's AudioOutput survives a Run teardown" (`Step` ×3, `RequestClose`, `Step` → false; zero validation messages). `Examples.Cli` ctest through `CheckUsage.cmake`.
- [ ] **Step 2: Run** `--ctest-regex "Shell|Examples"` → FAIL. **Implement.** Run `MinimalViewer <sample.prm>` by hand: the browser lists the six entries, double-click on the palette shows swatches, on the image shows the 4×4 picture, on the clip shows the waveform and plays through Media's output; take a screenshot into the session scratchpad.
- [ ] **Step 3: Run** → PASS, `check.py` full green. **Commit** `feat(examples): Add PrismUi and wire Prism and Media into MinimalViewer`

## Task 9: Docs, spec drift, README, CLAUDE.md

**Files:**
- Modify: `docs/superpowers/specs/2026-10-30-onyx-v2-design.md` (§4: Exchange's deps mention cgltf and stb as PRIVATE; §10: the final `MediaSource`/`AudioOutput`/`MediaView` shapes and the "video is an entry" rule; §11: `--first`, `cli.ambiguous`; §12: `AddExporters`; §13: Prism's section list and the two settings keys; §14: MinimalViewer "about twenty-five lines" and `OnyxCli` as built; §16 W4 location notes like the W3 ones), `README.md` (Status: Media, Exchange, Cli, Prism, the new suites; the embedding section's host contract gains the audio output line; consuming: FFmpeg), `CLAUDE.md` (Tools: the new suites, `ONYX_WITH_MEDIA` needs FFmpeg; Where to look unchanged), `tools/README.md` (verify T1's note), `CHANGELOG.md` (an `Unreleased` section listing W4 if the file has one for W3; otherwise leave it for W5)

- [ ] **Step 1: Read** the eight task reports in the SDD workspace (the controller passes their paths) and `git log --stat` of the wave; every sentence written must match a signature in the tree (`grep` it).
- [ ] **Step 2: Edit**; run `py -3 tools/lint.py` (narration rule) and `py -3 tools/check.py --preset windows-debug --jobs 4 --no-build --no-test` (configure + lint).
- [ ] **Step 3: Commit** `docs: Record the W4 Media, Cli, Exchange and Prism surfaces`

---

## Wave gate (spec §16)

- Prism opens, decodes, renders (its image through the stock Image viewer, its palette through PrismUi) with **zero SDK edits logged during T7 and T8** — the ledger records every `NEEDS_CONTEXT` and the SDK task it became.
- CLI goldens green (`Tests/Cli/Goldens`, OnyxBox and Prism).
- Full `check.py` green on Windows with the GPU suites running; Linux CI green needs a push and stays Jean's item (WSL lacks the X11 and FFmpeg dev packages).
