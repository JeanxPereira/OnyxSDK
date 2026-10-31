# Onyx v2 W5: Repo and Release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the v2 rewrite: delete the v1 tree, settle the deferred findings of the W4 review that matter before a release, write the three durable docs, turn the changelog into the 2.0.0 entry, and leave `v2` ready to merge and tag.

**Architecture:** No new layer and no new dependency. Three Core helpers replace copies in Cli, Render and Ui (`ResolveEntry` returns a status, `EntryAccess` opens and reads entries, `Paths` converts UTF-8). `TexturePool` gains a streaming path that never waits on the queue. `IAudioOutput` learns who is playing. Everything else is a bounded fix with the test that fails without it.

**Tech Stack:** C++20, CMake 3.28, doctest, Vulkan 1.3 through volk and VMA, ImGui, FFmpeg 7 (LGPL build on Windows), miniaudio, cgltf.

**Spec:** `docs/superpowers/specs/2026-10-30-onyx-v2-design.md` (§15 Repository, §16 W5 row) and the W4 final review (archived in the session scratchpad `sdd-archive-w4/`; its deferred list is reproduced task by task below).

## Global Constraints

- Layers compose and edges point down only: `Core < Gpu < Render < Ui < Shell < Media`, with `Cli`, `Exchange` and `TestKit` over Core. Public headers live in `Source/<Layer>/include/Onyx/<Layer>/` and compile standalone.
- Forbidden includes per layer as `tools/lint.py` enforces them. Core, Cli, Exchange and TestKit never include `windows.h`.
- No singleton behind `Get()`/`Instance()`; the only process-wide object is `Log::Default()`.
- English only; PascalCase files and types; no comment narrates a task, review, wave or previous version.
- Nothing under `Source/`, `Examples/` or `Tests/` names a game (`gow gowr gow1 gow2 ragnarok wad vag vpk pss`).
- Sources are globbed: a new `.cpp` or `.h` needs no CMake edit. Two tasks never edit the same CMake file.
- Commits: Conventional Commits, imperative Sentence-case subject of **at most 72 characters**, no attribution to AI anywhere. Never push; never merge into `main`.
- `py -3 tools/check.py --preset windows-debug` is the verdict. Before any build check free memory; under 3 GB free use `--jobs 2`.
- Existing CLI goldens in `Tests/Cli/Goldens` stay byte-identical unless a task says otherwise.
- The v1 tree is reachable at tag `v1.3.3`; nothing in the repository refers to `legacy/` after Task 1.

## Review Focus

1. A non-ASCII file name on the Windows command line (`OnyxCli decode café.obx image`) opens the file; today the narrow argv is read as the ANSI code page. Pinned by Task 2.
2. `decode --to gltf --out dir/x.gltf` with `dir` unwritable leaves no half-written pair and fails with `exchange.gltf.write`. Pinned by Task 3.
3. Two audio entries open side by side; pressing Play on the second makes the first show Play again with its position at zero, instead of both tracking the one output. Pinned by Task 5.
4. `ctest -j 8` runs every suite at once and no two suites collide on a temp file name. Pinned by Task 10.
5. A 1080p clip at 60 fps draws without a full queue wait per frame. Pinned by Tasks 5 and 9.

---

### Task 1: Delete the v1 tree and the old brand

**Files:**
- Delete: `legacy/` (322 files), `.github/Logo.png`, `dist/Glyph.svg`, `dist/Logo.svg`, `dist/Logo.afdesign`, `docs/design/2026-08-18-onyx-v1-architecture.md`, `docs/design/2026-08-19-public-surface-audit.md`, `docs/design/2026-10-16-generic-normal-map-encoding.md`, `docs/design/appearance-system.md`, `docs/gltf-validation.md`
- Create: `dist/Logo-1024.png` (copy of `D:\Design\OnyxSDK-light-square-1024.png`, the logo source)
- Modify: `tools/lint.py:12`, `tools/lint_test.py:39`, `CLAUDE.md`, `.gitattributes`

**Interfaces:**
- Produces: a tree where `git grep -i legacy -- ':!third_party' ':!docs/superpowers' ':!docs/design/2026-10-30-survey' ':!docs/design/2026-10-30-refactor-survey.md' ':!CHANGELOG.md' ':!Tests'` finds nothing (`LegacySize` is an ImGui field and stays).

- [ ] **Step 1: Delete the files** with `git rm -r legacy .github/Logo.png dist docs/gltf-validation.md` and the four `docs/design` files above. The survey (`docs/design/2026-10-30-refactor-survey.md` and `2026-10-30-survey/`) stays: it is the reason v2 exists. `docs/superpowers/` stays whole.

- [ ] **Step 2: Add the logo source** as `dist/Logo-1024.png`; `.gitattributes` already marks `*.png` binary. Drop the `*.afdesign binary` line.

- [ ] **Step 3: Lint**: remove `"legacy"` from `SKIP_DIRS` in `tools/lint.py:12`; in `tools/lint_test.py:39` the skip fixture becomes `third_party/Old.cpp`. `py -3 tools/lint_test.py` still prints `ok 6`.

- [ ] **Step 4: CLAUDE.md**: line 3 drops "rebuilt as v2" ("Onyx is a C++20 SDK for game-asset tools in nine layered static libraries …"). Rule 9 becomes: "`docs/architecture.md`, `docs/consuming.md` and `docs/modules.md` describe the shipped code; a change to a public contract updates the one that documents it." The "Where to look" table drops the `v1 reference` row and gains three rows for those docs (the files arrive in Task 8; the rows may point ahead).

- [ ] **Step 5: Verify**: `py -3 tools/check.py --lint-only` exits 0; the `git grep` in Interfaces prints nothing; `git ls-files legacy dist .github` lists `dist/Logo-1024.png` and `.github/workflows/ci.yml` only.

- [ ] **Step 6: Commit**: `chore: Delete the v1 tree, its design notes and the old brand files`.

---

### Task 2: Entry resolution with a status, shared entry access, UTF-8 arguments

**Files:**
- Modify: `Source/Core/include/Onyx/Core/Cli/Resolve.h`, `Source/Core/Cli/Resolve.cpp`, `Source/Core/include/Onyx/Core/Cli/Run.h` (contract comment), `Source/Core/include/Onyx/Core/Paths.h` and its `.cpp`
- Create: `Source/Core/include/Onyx/Core/Modules/EntryAccess.h`, `Source/Core/Modules/EntryAccess.cpp`
- Modify: `Source/Cli/Common.h`, `Source/Cli/Common.cpp`, `Source/Cli/Decode.cpp`, `Source/Cli/Extract.cpp`, `Source/Cli/Probe.cpp`, `Source/Cli/List.cpp` (paths from argv), `Source/Render/CliVerbs.cpp`, `Source/Ui/Viewers/Opening.cpp:24` (delete the local `FileOf` only), `Source/Shell/ShellCli.cpp`, `Source/Shell/include/Onyx/Shell/Shell.h`, `Examples/OnyxCli/Main.cpp`
- Test: `Tests/Core/Resolve.cpp` (new), `Tests/Core/EntryAccess.cpp` (new), `Tests/Cli/Verbs.cpp`, `Tests/Render/CliVerbs.cpp` (or the existing render verb test file), `Tests/Shell/Unit/*.cpp` for `RouteArgs`

**Interfaces:**
- Produces (Core, `Onyx/Core/Cli/Resolve.h`):
  ```cpp
  enum class ResolveStatus : uint8_t { Found, NotFound, Ambiguous, BadPath };
  struct Resolution {
      ResolveStatus status = ResolveStatus::NotFound;
      const Domain::AssetEntry* entry = nullptr;   // non-null only when Found
      std::vector<Modules::NodePath> matches;      // every match when Ambiguous
  };
  Resolution ResolveEntry(const Modules::Document& doc, std::string_view target, bool first);
  // Writes the one line a verb prints for a failed resolution, then returns false; true when Found.
  //   Ambiguous: err << "cli.ambiguous: N entries are named 'x': @0, @2\n"   (bytes as today)
  //   BadPath:   err << "invalid node path: @x\n"                            (nothing on out)
  //   NotFound:  out << "unknown entry: x\n"
  bool ReportResolution(const Resolution& r, std::string_view target, std::ostream& out, std::ostream& err);
  ```
- Produces (Core, `Onyx/Core/Modules/EntryAccess.h`):
  ```cpp
  namespace Onyx::Modules {
  // Null when `range.fileIndex` names a slot the document does not hold.
  std::shared_ptr<const Vfs::IFile> FileOf(const Document& doc, const Domain::ByteRange& range);
  // A reader over the entry's bytes; nullopt when FileOf is null.
  std::optional<Vfs::Reader> ReaderOf(const Document& doc, const Domain::AssetEntry& entry);
  // Opens `path` for the guard's lifetime and closes it on destruction. ok() is false when no module accepts it.
  class ScopedDocument {
  public:
      ScopedDocument(Workspace& ws, const std::filesystem::path& path, std::string_view moduleHint);
      ~ScopedDocument();
      ScopedDocument(const ScopedDocument&) = delete;
      ScopedDocument& operator=(const ScopedDocument&) = delete;
      bool ok() const;
      Document& doc();
      DocumentId id() const;
  };
  // Builds the decode context from the entry's range and the workspace settings, then DecodeScene.
  std::unique_ptr<Domain::Scene> DecodeSceneOf(Workspace& ws, Document& doc, const Domain::AssetEntry& entry,
                                               Services::Progress& progress);
  }
  ```
- Produces (Core, `Onyx/Core/Paths.h`): `std::filesystem::path Paths::FromUtf8(std::string_view)` and `std::string Paths::ToUtf8(const std::filesystem::path&)` (same bodies as `Ui/PathText.h`, which stays as is).
- Contract change (Core, `Run.h`): "`argv` is UTF-8 on every platform; a verb builds paths with `Paths::FromUtf8`."
- Shell: `Shell::Run(int, char**)`, `RunCli` and `RouteArgs` on Windows ignore the narrow strings and rebuild UTF-8 arguments from `GetCommandLineW` + `CommandLineToArgvW` (Shell may include `windows.h`); elsewhere argv is passed through. `Examples/OnyxCli/Main.cpp` uses `wmain` under `_WIN32` and converts each `wchar_t*` through `std::filesystem::path(w).u8string()` (no `windows.h`).

- [ ] **Step 1: Write the failing tests** in `Tests/Core/Resolve.cpp`: `"ResolveEntry reports Found, NotFound, Ambiguous and BadPath"` over an OnyxBox document with two entries named `dup` (assert `status`, `entry`, `matches.size() == 2`, and `first == true` gives `Found` with the first depth-first match); `"ReportResolution prints the ambiguous line byte for byte"` (the exact string above). In `Tests/Core/EntryAccess.cpp`: `FileOf` null on an out-of-range `fileIndex`; `ReaderOf` reads the entry's bytes; `ScopedDocument` closes on scope exit (`ws.Get(id) == nullptr` after); `DecodeSceneOf` returns a scene for the OnyxBox mesh and null for the image.

- [ ] **Step 2: Run** `Core.Unit` and confirm the new cases fail to compile or fail.

- [ ] **Step 3: Implement** the Core interfaces above. `Modules::FindAllByName` and the depth-first order `Resolve` uses must agree; add the case `"FindAllByName lists matches in depth-first order"` with a nested duplicate.

- [ ] **Step 4: Replace the copies**: `Cli/Common.{h,cpp}` drop `OpenDocument`, `OpenOrReport`'s open half and `FileOf` in favour of `ScopedDocument`/`Modules::FileOf` (keep `OpenOrReport` as the message printer over a `ScopedDocument`); `Decode.cpp:180-186` and `Render/CliVerbs.cpp:166-173` use `ReportResolution` and no longer sniff `"cli.ambiguous"`; `Render/CliVerbs.cpp` drops `OpenedDocument` and `DecodeScene` for the Core ones; `Ui/Viewers/Opening.cpp:24` deletes its `FileOf` and includes `EntryAccess.h` (nothing else in that file changes).

- [ ] **Step 5: Render verb hardening** in `Render/CliVerbs.cpp`: `--time` rejects nan and inf (usage); `--width`/`--height` above 16384 is usage; the device is destroyed before `png->write` runs (scope `device` and `RenderToImage` in a block that yields the image); the unknown-entry path prints `PrintDiags(doc.diags, out)` like `decode`. Tests: `"render rejects a non-finite --time"`, `"render refuses a 20000 pixel width"`, `"render reports render.noScene for an image entry"` (`kPartial`, line starts `render.noScene:`), `"render reports cli.ambiguous without --first"`.

- [ ] **Step 6: `extract --first` pin**: test `"extract accepts --first and ignores it"` (same output with and without). Bad path through `decode`: test `"decode @x prints only the invalid node path line"` (nothing on `out`).

- [ ] **Step 7: UTF-8 arguments**: `Paths::FromUtf8/ToUtf8` in Core with the test `"Paths::FromUtf8 round-trips a non-ASCII name"` (`u8"caf\u00e9-\u00fc"`); the four stock verbs and `render` build their file path with `Paths::FromUtf8(args.positional[0])` and `--out` the same way; `Shell::Run`, `RunCli`, `RouteArgs` rebuild argv on Windows; `OnyxCli` uses `wmain`. Test (Cli): `"decode opens a file whose name is non-ASCII when argv is UTF-8"` writes an `.obx` under a `caf\u00e9` directory and runs the verb with the UTF-8 bytes. Test (Shell.Unit): `"RouteArgs opens a non-ASCII path given in UTF-8"` on Windows (skip elsewhere).

- [ ] **Step 8: Verify**: `py -3 tools/check.py --preset windows-debug --ctest-regex "Core|Cli|Render|Shell.Unit|Examples"` exits 0; every file in `Tests/Cli/Goldens` unchanged (`git status --short Tests/Cli/Goldens` empty).

- [ ] **Step 9: Commit** in three: `feat(core): Return a status from ResolveEntry and share entry access`, `fix(render): Harden the render verb's arguments and report its diagnostics`, `fix(cli): Read command-line arguments as UTF-8 on Windows`.

---

### Task 3: Exchange writes exact, atomic and path-aware

**Files:**
- Modify: `Source/Exchange/Gltf.cpp:166-178,490-515`, `Source/Exchange/include/Onyx/Exchange/Gltf.h`, `Source/Exchange/Png.cpp`, `Source/Exchange/Wav.cpp`
- Test: `Tests/Exchange/Gltf.cpp`, `Tests/Exchange/Png.cpp`, `Tests/Exchange/Wav.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `WriteGltf` behaviour below; no signature change.

- [ ] **Step 1: Write the failing tests** in `Tests/Exchange/Gltf.cpp`: `"index and joint accessors carry no min or max"` (parse the written file with `cgltf_parse` on the bytes; `has_min == false` on the index accessor and on JOINTS_0; POSITION keeps both); `"a .gltf whose directory is a file leaves no .bin behind"` (out path `<tmpfile>/x.gltf`; result code `exchange.gltf.write`; no `x.bin` beside it); `"a .gltf under a non-ASCII directory is written"` (`caf\u00e9/x.gltf`, read back through `std::ifstream` and `cgltf_parse`). In `Png.cpp` and `Wav.cpp`: `"WritePng reports exchange.png.write for an unwritable path"` and `"WriteWav reports exchange.wav.write for an unwritable path"`.

- [ ] **Step 2: Run** `Exchange.Unit`; the new cases fail.

- [ ] **Step 3: Implement**: `addAccessor` takes `bool bounded`; the index accessor and JOINTS_0 pass `false` and set `has_min = has_max = false` (glTF requires bounds for POSITION only; a float `min`/`max` cannot hold an index above 2^24 exactly). Write order: the `.bin` first, then the document; when the document write fails, remove the `.bin` and return `exchange.gltf.write`. Replace `cgltf_write_file` with `cgltf_write` into a `std::string` sized by its first call, written through `std::ofstream(out, std::ios::binary)`, so the path is never narrowed. `Gltf.h` states: "A `.gltf` output writes `<stem>.bin` beside it and replaces any file of that name."

- [ ] **Step 4: Verify**: `py -3 tools/check.py --preset windows-debug --ctest-regex "Exchange|Examples.Cli"` exits 0; `Tests/Cli/Goldens` unchanged.

- [ ] **Step 5: Commit**: `fix(exchange): Write glTF pairs atomically and keep accessor bounds exact`.

---

### Task 4: Media edge cases, build guards

**Files:**
- Modify: `Source/Media/MediaSource.cpp` (`Impl::Run`, `ApplySeek`, the EOF path), `Source/Media/include/Onyx/Media/MediaSource.h` (comment), `Source/Media/Decoder.cpp:116-123`, `Source/Media/AudioOutput.cpp`, `Source/Media/include/Onyx/Media/AudioOutput.h`, `Source/Media/MediaView.cpp:45-60,138-147`, `Source/Media/include/Onyx/Media/MediaView.h:15-22`, `cmake/Deps/Ffmpeg.cmake`, `Examples/MinimalViewer/Main.cpp:11,24`, `Tests/Shell/Window/Shell.cpp:21,357`
- Test: `Tests/Media/MediaSource.cpp`, `Tests/Media/AudioOutput.cpp`, `Tests/Media/MediaView.cpp`, `Tests/Media/Gpu/MediaView.cpp:48,69`

**Interfaces:**
- Produces (`AudioOutput.h`): `double Latency() const;` (seconds the device buffers ahead of what it has played: `internalPeriodSizeInFrames * internalPeriods / sampleRate`, 0 when no device) and `bool DeviceRunning() const;`.
- Produces (`MediaView.h`): the source opens on a workspace job, not on the first Draw; `Draw` shows "Opening…" until the job completes; `Opened()` reports `true` once the open result is in.

- [ ] **Step 1: Write the failing tests** in `Tests/Media/MediaSource.cpp`: `"a seek queued at the end of the clip is applied before End"` (Play a 0.2 s clip, `Seek(0.0)` repeatedly for 300 ms, then require the source is not `Ended` with `Position() >= duration` while a seek is pending; design the timing with the transport's command queue, which is internal to the layer and may be included from the test); `"the steady clock takes over 250 ms after the audio starves"` (a clip with 0.5 s video and 0.1 s audio from the Support builder: `UsesAudioClock()` becomes false within 2 s and `Position()` reaches the video end, then `Ended`); `"a read error is logged as media.read and ends the stream"` (a clip truncated mid-packet: the `Log::Default()` ring holds a `media.read` line; the state reaches `Ended`); `"a refused seek is logged as media.seek and playback continues"` (an `IFile` wrapper that fails every read after the probe; one `media.seek` line; no hang, no crash). In `AudioOutput.cpp`: `"Stop stops the device"` (`DeviceRunning()` false after Stop, true while Playing), `"Latency is non-negative and below one second"`. In `Tests/Media/MediaView.cpp`: `"Open does not block the first Draw"` (first `Draw` returns in under 50 ms while the clip is still opening; `Opened()` turns true within 5 s).

- [ ] **Step 2: Run** `Media.Unit` and confirm the new cases fail.

- [ ] **Step 3: Implement**: in `Impl::Run`, drain pending seeks before `transport.End()`; `Position()` subtracts `AudioOutput::Latency()` while on the audio clock (clamped at 0); `AudioOutput::Play` copies the clip into a local before taking `mutex`, then swaps; `Stop` calls `ma_device_stop` under `deviceMutex` (outside `mutex`) and `Play` restarts it; `Decoder::Feed` distinguishes `AVERROR_EOF` from other codes: anything else logs `media.read` with `av_err2str` text and ends the read; `MediaSource.h` names the limits (newest plus four decoded frames; 256 packets or 32 MiB of compressed read-ahead). `MediaView::Open` submits the `MediaSource::Open` to `ui.workspace.Jobs()` on the decode lane; the result lands under a mutex and `Draw` picks it up. Rewrap the `MediaView.h` comment to 100 columns. Widen the lag test's tolerance to at least 500 ms and the `Tests/Media/Gpu/MediaView.cpp` deadlines to 20 s.

- [ ] **Step 4: Build guards**: `cmake/Deps/Ffmpeg.cmake` fails the configure when the DLL glob is empty ("no FFmpeg DLLs under <root>/bin") and asks pkg-config for `libavformat>=60 libavcodec>=60 libavutil>=58 libswscale>=7 libswresample>=4` (FFmpeg 6.0). `ONYX_WITH_MEDIA` is defined only when the component is on; the two `#if ONYX_WITH_MEDIA` become `#ifdef ONYX_WITH_MEDIA`.

- [ ] **Step 5: Verify**: `py -3 tools/check.py --preset windows-debug --ctest-regex "Media|Shell|Examples"` exits 0. Record which tests needed a real audio device.

- [ ] **Step 6: Commit** in two: `fix(media): Settle seeks before End, log read errors and open views off-thread`, `build(media): Fail on a missing FFmpeg runtime and pin the pkg-config floor`.

---

### Task 5: Streamed textures and owned audio playback

**Files:**
- Modify: `Source/Ui/include/Onyx/Ui/TexturePool.h`, `Source/Ui/TexturePool.cpp`, `Source/Ui/include/Onyx/Ui/AudioOutput.h` (the `IAudioOutput` interface, wherever it is declared), `Source/Ui/Viewers/AudioView.cpp`, `Source/Ui/Viewers/Opening.cpp` (the Pump continuation only, not the top `FileOf`), `Source/Ui/Viewers/TextView.cpp`, `Source/Shell/Shell.cpp:111-127,154-159`, `Source/Shell/include/Onyx/Shell/Shell.h`, `Source/Media/AudioOutput.cpp` (the `IAudioOutput` overrides only), `Examples/Prism/Ui/*.cpp` if a fake output exists there
- Test: `Tests/Ui/Gpu/TexturePool.cpp` (or the existing Ui.Gpu texture test), `Tests/Ui/AudioView.cpp`, `Tests/Ui/ViewerOpening.cpp`, `Tests/Ui/TextView.cpp`, `Tests/Shell/Unit/Shell.cpp`, `Tests/Shell/Window/Shell.cpp`

**Interfaces:**
- Produces (`TexturePool.h`):
  ```cpp
  // Streams a new frame into a texture created by Create, without waiting on the queue: each streamed
  // id owns a ring of three images, each bound once; Stream uploads into the image whose last use the
  // queue has completed (waiting only when all three are in flight) and returns that image's id, which
  // replaces `id` for the next Draw. Remove on any id of the ring removes the whole ring.
  Result<ImTextureID> Stream(ImTextureID id, const Domain::Texture& texture);
  ```
  `Update` keeps its stalling contract and comment.
- Produces (`IAudioOutput`): `Play(const Domain::Audio& audio, std::string_view owner)` and `std::string_view Owner() const` (empty when nothing plays). A view treats the output as its own only while `Owner() == its id`; `Play` from another owner takes the output over.
- Produces (`Shell`): `SetAudioOutput` called during a frame takes effect at the next frame boundary; the previous output is stopped then.

- [ ] **Step 1: Write the failing tests**. Ui.Gpu: `"Stream rotates through three ids and the first returns on the fourth frame"` (ids from four consecutive Streams: `a, b, c, a`; drawing each returned id in a submitted frame passes validation with zero messages); `"Remove on a streamed id frees the ring"` (no leak report from VMA on device destruction, and `Update`/`Stream` on it fail with `ui.texture.size`). Ui.Unit: `"two AudioViews share one output and only the owner shows Stop"` (fake output records `owner`; after Play on view B, view A's frame shows Play and its position is 0); `"a null payload reports ui.open.decode and opens no tab"` (a decoder returning null: after Pump, `doc.diags` holds `ui.open.decode`, panel tab count unchanged); `"TextView counts CRLF endings as one line break"` (`"a\r\nb\r\n"` is two lines, no `\r` in either). Shell.Unit: `"SetAudioOutput inside a Draw replaces the output on the next frame"` (a panel calls it during Draw; `Context().audio` is the old pointer for the rest of that frame and the new one on the next; the old one's Stop was called); Shell.Window: `"the shell stops audio before it destroys the views"` (a recording fake output: `Stop` precedes the view's destructor in the recorded order).

- [ ] **Step 2: Run** `Ui.Unit`, `Ui.Gpu`, `Shell.Unit`, `Shell.Window`; the new cases fail.

- [ ] **Step 3: Implement** `Stream` as the Interfaces block states, over the existing `UploadChecked` for the first fill and `UploadContext` copies afterwards, with the ring's per-image descriptor bound through `ITextureBinder::Bind` once. The last-use value of a slot is the timeline value of the frame that drew it; take it from `Gpu::Queue::LastSubmitted()` at Stream time (a conservative bound). `AudioView` passes `m_id` as the owner and reads `Owner()` before `Playing()`. `Opening.cpp`'s continuation reports `ui.open.decode` through the document's diags with the entry name and kind. `TextView` splits on `\n` and strips a trailing `\r`. `Shell::SetAudioOutput` stores a pending output applied where the frame builds its `UiContext`.

- [ ] **Step 4: Verify**: `py -3 tools/check.py --preset windows-debug --ctest-regex "Ui|Shell|Media|Examples"` exits 0 with zero validation messages.

- [ ] **Step 5: Commit** in three: `feat(ui): Stream hot textures through a ring that never waits on the queue`, `fix(ui): Let one audio output serve several views and report null decodes`, `fix(shell): Apply SetAudioOutput at the frame boundary and stop audio first`.

---

### Task 6: Prism pins and the Prism test target

**Files:**
- Modify: `Examples/Prism/Module.cpp` (the Reader copy, the duplicate-palette path), `Examples/Prism/Writer.cpp`, `Examples/Prism/include/Onyx/Prism/Writer.h` (or wherever `Writer` is declared), `Examples/Prism/Ui/PaletteView.cpp` (the hex measurement), `Tests/Prism/CMakeLists.txt`
- Test: `Tests/Prism/Module.cpp`, `Tests/Prism/Decoders.cpp`, `Tests/Prism/Writer.cpp` (new)

**Interfaces:**
- Produces: `Writer::Build()` returns an empty vector when a section or the file would exceed `UINT32_MAX` bytes (documented on `Build`).

- [ ] **Step 1: Write the failing tests**: `"a second palette is reported as prism.palette.duplicate and the first one is used"`; `"Build returns empty when a range wraps past 4 GiB"` (`AddRange("r", kind, 0xFFFFFFF0u, 0x20u)`); one case per `prism.*` diagnostic code the module can emit that has no case yet (list them from `grep -o 'prism\.[a-z.]*' Examples/Prism/Module.cpp | sort -u` against the tests), plus `prism.gain` applied to a decoded clip and `prism.transparentIndexZero` in both states if either is missing.

- [ ] **Step 2: Run** `Prism.Unit`; the new cases fail.

- [ ] **Step 3: Implement**: the decoder that copies `ctx.reader` takes it by reference; `Writer` tracks overflow and `Build` returns empty on it; `PaletteView` measures `"#FFFFFFFF"`.

- [ ] **Step 4: Test target**: `Tests/Prism/CMakeLists.txt` goes through `onyx_add_tests(Prism LINK Onyx::ExamplePrism Onyx::TestKit)` the way `Tests/Exchange/CMakeLists.txt` does, so the standalone header check and the `Tests/Support` include apply; keep the `ONYX_GOLDEN_DIR` and `ONYX_SOURCE_DIR` definitions if the helper does not add them.

- [ ] **Step 5: Verify**: `py -3 tools/check.py --preset windows-debug --ctest-regex "Prism|Examples.Cli"` exits 0; `Tests/Cli/Goldens` unchanged.

- [ ] **Step 6: Commit**: `fix(prism): Pin the palette, writer and view edge cases and share the test rig`.

---

### Task 7: CHANGELOG 2.0.0

**Files:**
- Modify: `CHANGELOG.md:1-64`

- [ ] **Step 1: Rewrite the `## Unreleased — 2.0.0` block** as `## v2.0.0 — Unreleased` (the date is filled when Jean tags). No wave numbers anywhere. Shape: one paragraph (break-and-port; no compatibility with 1.x; the v1 tree is at tag `v1.3.3`), then `### Added` grouped by layer (Core, Gpu, Render, Ui, Shell, Media, Cli, Exchange, TestKit, Examples, Tools), `### Changed` against v1 (library over framework, stateless VFS, type catalog as an object, module contract with capabilities, settings with a schema, no singletons, CLI verbs with byte-exact goldens, the LGPL FFmpeg build on Windows), `### Removed` (the v1 public surface, `legacy/`, the old logo and brand files, the v1 design notes), `### Known gaps` (what Tasks 2 to 6 left out, from their reports). Every bullet names a public type, header, verb, option or test suite that exists in the tree at that moment (verify each with `git grep`). The three bullets that today sit under the last heading but describe Shell, Examples and the Ui suites move into their layers.

- [ ] **Step 2: Verify**: `py -3 tools/check.py --lint-only` exits 0; the unslop phrase scanner on `CHANGELOG.md:1-120` reports zero hard hits.

- [ ] **Step 3: Commit**: `docs: Write the 2.0.0 changelog entry`.

---

### Task 8: The three durable docs

**Files:**
- Create: `docs/architecture.md`, `docs/consuming.md`, `docs/modules.md`
- Modify: `README.md` ("Where to look": the design row points at `docs/architecture.md`, with the spec one row below as "How it was decided"), `tools/README.md` (one sentence: public-header hygiene for private dependencies is enforced by `cmake/StandaloneHeaders.cmake`, which `lint.py` cannot express), `docs/superpowers/specs/2026-10-30-onyx-v2-design.md` §4 (add cgltf to the FetchContent sentence)

**Interfaces:**
- Consumes: the final public headers after Tasks 2 to 6 (`ResolveStatus`, `EntryAccess.h`, `TexturePool::Stream`, `IAudioOutput::Owner`).

- [ ] **Step 1: `docs/architecture.md`** (about 250 lines): the nine layers and their edges; what each public contract is for (`Vfs`, `TypeCatalog`, `Domain`, `IGameModule` and `DecoderRegistry`, `Workspace` and jobs, `Settings`, `Cli::VerbTable` and exporters, `Gpu::Device` own or adopt, `SceneRenderer` and `RenderToImage`, `UiContext`, panels and viewers, `Shell`, `MediaSource`); the threading rules (main-thread marker, decode lane, Media's one decode thread, the queue mutex); error reporting (`Result`, the `<layer>.<code>` tables); lifetimes (device before pool before views); what is deliberately absent (singletons, a plugin ABI, game knowledge).

- [ ] **Step 2: `docs/consuming.md`** (about 150 lines): FetchContent with a tag, the `ONYX_WITH_*` options and what each turns off, the three shapes (Shell host, Ui in your own window as `Examples/Embedded` does, Core-only CLI as `Examples/OnyxCli`), `Version.h` from the build directory, the FFmpeg runtime on Windows (`onyx_install_media_runtime`, LGPL build, `ONYX_FFMPEG_WIN_URL`) and the Linux packages, how `check.py` and the presets run outside this repository, and the UTF-8 argv contract.

- [ ] **Step 3: `docs/modules.md`** (about 200 lines): the module class walked through with Prism's code (probe, walk, types, decoders, settings schema, module state, `Any` with a capability, schema records), the diagnostic rules (report on the entry, never throw), the tests a module ships (TestKit fixtures, tree and CLI goldens, `Standalone.cpp`), and how a viewer or command registers for a module's type.

- [ ] **Step 4: Verify** every file, type, function and option named in the three docs exists (`git grep` each identifier; a list of the checked names goes in the report); `py -3 tools/check.py --lint-only` exits 0; unslop phrase scanner zero hard hits on each file.

- [ ] **Step 5: Commit**: `docs: Add the architecture, consuming and modules guides`.

---

### Task 9: MediaView streams its frames

**Files:**
- Modify: `Source/Media/MediaView.cpp:55-70`, `Source/Media/include/Onyx/Media/MediaView.h`
- Test: `Tests/Media/Gpu/MediaView.cpp`

**Interfaces:**
- Consumes: `TexturePool::Stream` from Task 5; the off-thread open from Task 4.

- [ ] **Step 1: Write the failing test**: `"played frames stream without a queue wait"` (after the first frame, each `Draw` that takes a new frame changes `Texture()` to a ring id; twenty played frames under validation with zero messages; the three ids seen repeat).

- [ ] **Step 2: Run** `Media.Gpu`; the case fails (today `Texture()` never changes).

- [ ] **Step 3: Implement**: `MediaView` keeps the id `Stream` returns and draws with it; `Update` is no longer called per frame. The header says frames are streamed.

- [ ] **Step 4: Verify**: `py -3 tools/check.py --preset windows-debug --ctest-regex "Media"` exits 0.

- [ ] **Step 5: Commit**: `perf(media): Stream video frames through the texture ring`.

---

### Task 10: Unique temp names across the suites

**Files:**
- Create: `Tests/Support/TempDir.h`
- Modify: every `temp_directory_path()` use under `Tests/` with a fixed name (46 sites at the time of writing: `Tests/Cli/*.cpp`, `Tests/Core/*.cpp`, `Tests/Consumer/CoreOnly/Main.cpp`, `Tests/Media`, `Tests/Shell`, `Tests/Ui`, `Tests/Exchange`, `Tests/Prism`, `Tests/Render` as found by `git grep -n temp_directory_path -- Tests`)

**Interfaces:**
- Produces:
  ```cpp
  namespace Onyx::Test {
  // A directory of its own under the system temp root, `onyx-<tag>-<pid>-<n>`, removed on destruction.
  class TempDir {
  public:
      explicit TempDir(std::string_view tag);
      ~TempDir();
      const std::filesystem::path& path() const;
      std::filesystem::path operator/(std::string_view name) const;
  };
  }
  ```

- [ ] **Step 1: Write the failing test** in `Tests/Core/TempDir.cpp`: two `TempDir("x")` in one process differ; the directory exists while alive and is gone after; `operator/` joins.

- [ ] **Step 2: Implement** `TempDir.h` (header-only, `<atomic>` counter, `GetCurrentProcessId`-free: use `std::this_thread::get_id` hashed or `getpid`/`_getpid` through `<process.h>`/`<unistd.h>`).

- [ ] **Step 3: Migrate** every fixed-name site to a `TempDir` (one per fixture or test case). Golden comparisons and `.obx`/`.prm` fixtures keep their file names inside the directory.

- [ ] **Step 4: Verify**: `ctest --test-dir build/windows-debug -j 4` twice in a row exits 0 both times, then the full `py -3 tools/check.py --preset windows-debug` exits 0.

- [ ] **Step 5: Commit**: `test: Give every suite its own temp directory`.

---

### Task 11: The verdict on both platforms

**Files:**
- Modify: `tools/check.py` only if it lacks a way to pass extra configure arguments (then add `--cmake-arg`, repeatable, documented in `tools/README.md`).

- [ ] **Step 1: Windows**: `py -3 tools/check.py --preset windows-debug --jobs 4` exits 0 with every suite listed; record suite count, case count and validation message count.

- [ ] **Step 2: Linux (WSL)**: `ONYX_NO_VALIDATION=1 python3 tools/check.py --preset linux-debug --cmake-arg -DONYX_WITH_SHELL=OFF --cmake-arg -DONYX_WITH_MEDIA=OFF` exits 0 (the WSL image has no X11 or FFmpeg development packages). The hosted Linux job with every component on runs on the first push and is Jean's.

- [ ] **Step 3: Lint**: `py -3 tools/lint.py` exits 0 and prints no game name and no forbidden include.

- [ ] **Step 4: Tag readiness**: no commit is created here. The report lists the exact commands Jean runs after merging: `git tag -a v2.0.0 -m "Onyx 2.0.0"` on the merge commit and the CHANGELOG date line to fill.
