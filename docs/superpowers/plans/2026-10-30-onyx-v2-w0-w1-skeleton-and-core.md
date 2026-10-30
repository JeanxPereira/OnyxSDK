# Onyx v2 — W0 Skeleton + W1 Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up the v2 repository skeleton (layered targets, per-component dependency fetch, `check.py`, CI) and deliver `Onyx::Core` complete with tests, the `Onyx::Cli` stock verbs, `Onyx::TestKit`'s Core half, and the OnyxBox module ported, so a Core-only consumer configures with no GPU/UI dependency.

**Architecture:** The v1 tree moves to `legacy/` (reference only, no target compiles it). A new root `CMakeLists.txt` declares nine layer targets, each in `Source/<Layer>/` with its own `include/Onyx/<Layer>/` as the only public include path. W1 fills `Core` by porting what the design keeps (jobs, events, diags, probe, workspace, settings, schema, ISO) and writing what it changes (stateless VFS, catalog as object, generic domain model, `Any` decoders and `Capabilities`, atomic settings, CLI verb contract).

**Tech Stack:** C++20, CMake 3.28+ (must configure under CMake 4.2), Ninja, MSVC (VS 18 via vcvars64) on Windows, Clang on Linux; glm, toml++ 3.4, lz4 1.9.4, doctest 2.4.11, stb; Python 3.14 for `tools/`.

**Spec:** `docs/superpowers/specs/2026-10-30-onyx-v2-design.md` (§4 layers, §5 Core, §11 Cli, §13 TestKit, §15 repository, §16 W0/W1 gates). Survey: `docs/design/2026-10-30-refactor-survey.md`.

## Global Constraints

- Branch `v2` (from `v1.3.3`). Commit per task with Conventional Commits, imperative Sentence-case subject ≤ 72 chars, explicit paths in `git add`, **no attribution line of any kind**. Never push.
- Build and test with `py -3 tools/check.py --preset windows-debug` (Task 3 creates it); before that, the commands each task names. Build dir is `build/<preset>`; the old `build/` from v1 is ignored and must not be deleted.
- Public headers live only under `Source/<Layer>/include/Onyx/<Layer>/`. Namespaces: `Onyx::Vfs`, `Onyx::Types`, `Onyx::Domain`, `Onyx::Modules`, `Onyx::Services`, `Onyx::Schema`, `Onyx::Cli`, `Onyx::TestKit`. The spec's `Core::X` shorthand means "in the Core layer".
- Core forbids `imgui`, `volk`, `vk_mem_alloc`, `vulkan/`, `GLFW/`, `windows.h` (in headers), `libav*`, `miniaudio`. `windows.h` is allowed in a Core `.cpp` only.
- No `static X& Get()` / `Instance()` singletons. The one process-wide object is `Services::Log::Default()`.
- No game name (`gow`, `gowr`, `gow1`, `gow2`, `ragnarok`, `wad`, `vag`, `vpk`, `pss`) anywhere under `Source/`, `Examples/`, `Tests/` (case-insensitive; the lint enforces it from Task 5).
- No comment narrates a task, milestone, fix round, review or previous version. English only. PascalCase file and type names; `.cpp`/`.h`.
- Every public header compiles standalone with only its layer's include path (Standalone test, Task 4).
- `legacy/` is read-only reference; nothing links or includes it; it is deleted in W5.
- Tests use doctest; test executables are `OnyxCoreTests`, `OnyxCliTests`, `OnyxTestKitTests`; ctest names are `Core.<Suite>` etc.

## Review Focus

1. A container that declares an entry size larger than the file: `Reader`/`SliceFile` must clamp, never read past `Size()`, and `extract` must report the entry failed (Task 7, Task 17).
2. A TOML settings file with a syntax error: the user's file is preserved as `<name>.broken.toml` and the next save does not overwrite it (Task 12).
3. Two `Reader`s over one `IFile` from two threads: both read correct bytes (Task 7).
4. A path with non-ASCII characters on Windows: `OsFile::Open` succeeds (Task 7).
5. A module decoder that throws: `DecodeAny`/`DecodeScene` return null with one Error diag, and `Capabilities` still reports the slot (Task 15).

---

## W0 — Skeleton

### Task 1: Move the v1 tree to `legacy/`

**Files:**
- Move: `Source/` → `legacy/Source/`, `Include/` → `legacy/Include/`, `Examples/` → `legacy/Examples/`, `Tools/` → `legacy/Tools/`, `Tests/*.cpp`, `Tests/CMakeLists.txt`, `Tests/Consumer/` → `legacy/Tests/…`, `CMakeLists.txt` → `legacy/CMakeLists.txt`, `cmake/` → `legacy/cmake/`, `CMakePresets.json` → `legacy/CMakePresets.json`
- Keep in place: `Tests/Golden/`, `third_party/`, `docs/`, `dist/`, `.github/`, `README.md`, `CHANGELOG.md`, `LICENSE`, `.clang-format`, `.editorconfig`, `.gitattributes`, `.gitignore`
- Create: `legacy/README.md` (five lines: what this is, that nothing builds it, that W5 deletes it)

- [ ] **Step 1: Move with `git mv`** so history follows. Use `git mv Source legacy/Source` etc. `Tests/Golden` stays: `git mv` only the listed test files and `Tests/Consumer`.
- [ ] **Step 2: Verify** `git status --short | grep -v '^R' | grep -v legacy/README.md` prints nothing, and `ls` at root shows `Tests/Golden`, `third_party`, `legacy`, no `Source`, no `Include`.
- [ ] **Step 3: Commit** `chore: move the v1 tree to legacy/ for reference during the v2 port`

### Task 2: Root build with components and per-dependency fetch

**Files:**
- Create: `CMakeLists.txt`, `CMakePresets.json`, `cmake/Warnings.cmake`, `cmake/Version.cmake`, `cmake/Version.h.in`, `cmake/Layer.cmake`, `cmake/Deps/Glm.cmake`, `cmake/Deps/TomlPlusPlus.cmake`, `cmake/Deps/Lz4.cmake`, `cmake/Deps/Doctest.cmake`, `cmake/Deps/Stb.cmake`
- Create: `Source/Core/CMakeLists.txt`, `Source/Core/include/Onyx/Core/Layer.h`, `Source/Core/Layer.cpp`; the same trio for `Cli`, `TestKit`, `Exchange`, `Gpu`, `Render`, `Ui`, `Shell`, `Media`

**Interfaces:**
- Produces: CMake function `onyx_add_layer(<Name> SOURCES … DEPS … PUBLIC_DEPS …)` in `cmake/Layer.cmake`: creates `Onyx_<Name>` STATIC + alias `Onyx::<Name>`, `target_include_directories(PUBLIC include/)`, applies `onyx_warnings()`, `target_precompile_headers` from `Source/<Name>/Pch.h` when present, C++20.
- Produces: options `ONYX_WITH_GPU`, `ONYX_WITH_UI` (forces GPU), `ONYX_WITH_SHELL` (forces UI), `ONYX_WITH_MEDIA` (forces UI), `ONYX_BUILD_TESTS`, `ONYX_BUILD_EXAMPLES`, `ONYX_BUILD_TOOLS`; the last three default `ON` iff `PROJECT_IS_TOP_LEVEL`, the first four default `ON`.
- Produces: `cmake/Deps/<Dep>.cmake` each defines `onyx_dep_<dep>()` that is idempotent (checks `TARGET`) and pins the version from the spec §3 (`glm 1.0.1`, `tomlplusplus v3.4.0`, `lz4 v1.9.4`, `doctest v2.4.11`, stb from `third_party/stb`). `FETCHCONTENT_UPDATES_DISCONNECTED ON`.
- Produces: `onyx_version()` configures `cmake/Version.h.in` into `${CMAKE_BINARY_DIR}/generated/Onyx/Core/Version.h` with `ONYX_VERSION_MAJOR 2`, `MINOR 0`, `PATCH 0`, `ONYX_VERSION_STRING "2.0.0"`; the generated dir is added to Core's PUBLIC includes.
- Produces: presets `windows-debug`, `windows-release` (Ninja, `CMAKE_CXX_COMPILER=cl`), `linux-debug`, `linux-release` (Ninja, clang++); `binaryDir` `${sourceDir}/build/${presetName}`; build presets of the same names; test presets `windows-debug`/`linux-debug` with `output-on-failure`.
- Produces: `Layer.h` declares `const char* Onyx::<Name>::LayerName();` returning the layer's name.

- [ ] **Step 1: Write `CMakeLists.txt`** with `cmake_minimum_required(VERSION 3.28)`, `set(ENV{VSLANG} 1033)` before `project(OnyxSDK VERSION 2.0.0 LANGUAGES CXX C)`, `cmake_policy(VERSION 3.28...4.2)`, the options above, `include(cmake/Warnings.cmake)`, `include(cmake/Layer.cmake)`, `include(cmake/Version.cmake)`, then `add_subdirectory(Source/Core)`, `Source/Cli`, `Source/TestKit`, `Source/Exchange`, and the four gated layers under their options; `enable_testing()` before `Tests/` and `Examples/` (`if(ONYX_BUILD_TESTS)`, `if(ONYX_BUILD_EXAMPLES)`; those directories arrive in later tasks, so guard with `if(EXISTS)` for this task only and remove the guard in Task 4/19).
- [ ] **Step 2: Write `cmake/Warnings.cmake`**: `onyx_warnings(target)` sets `/W4 /permissive- /utf-8 /Zc:__cplusplus` + `/wd4100` on MSVC, `-Wall -Wextra -Wpedantic -Wno-unused-parameter` elsewhere; warnings are not errors in W0.
- [ ] **Step 3: Write the nine layer directories** with `Layer.h`/`Layer.cpp` and a `CMakeLists.txt` calling `onyx_add_layer(<Name> SOURCES Layer.cpp)`; Core's additionally calls `onyx_dep_glm()`, `onyx_dep_tomlplusplus()`, `onyx_dep_lz4()` and links them `PUBLIC glm::glm` / `PRIVATE tomlplusplus::tomlplusplus lz4` ; Cli, TestKit, Exchange link `Onyx::Core`; Gpu links Core; Render links Gpu; Ui links Render; Shell and Media link Ui. TestKit additionally `onyx_dep_stb()`.
- [ ] **Step 4: Configure and build on Windows**: from PowerShell `cmd /c "call `"C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat`" >nul && cmake --preset windows-debug && cmake --build --preset windows-debug"`. Expected: all nine `Onyx_<Name>.lib` produced; `build/windows-debug/_deps` contains only `glm`, `tomlplusplus`, `lz4`, `doctest` sources.
- [ ] **Step 5: Configure with `-DONYX_WITH_GPU=OFF`** into `build/coreonly` and confirm the gated layers are absent from `cmake --build build/coreonly --target help`.
- [ ] **Step 6: Commit** `build: declare the v2 layer targets and per-component dependencies`

### Task 3: `tools/check.py` and `tools/lint.py`

**Files:**
- Create: `tools/check.py`, `tools/lint.py`, `tools/README.md`

**Interfaces:**
- Produces: `py -3 tools/check.py [--preset P] [--no-build] [--no-test] [--lint-only] [--ctest-regex R]`; default preset `windows-debug` on Windows, `linux-debug` elsewhere. On Windows it locates `vswhere.exe` under `%ProgramFiles(x86)%\Microsoft Visual Studio\Installer`, picks the latest VS with the C++ workload, and runs every cmake/ctest command inside `cmd /c "call vcvars64.bat && …"`. Steps: configure → build → ctest → lint; the first failing step ends the run; exit code is the step's exit code; it prints one line per step with elapsed seconds.
- Produces: `py -3 tools/lint.py [root]` exits 1 and prints `path:line: rule: detail` for: (a) forbidden includes per layer (table in the file, from Global Constraints and spec §4), scanning `Source/<Layer>/include/**` for all listed tokens and `Source/<Layer>/*.cpp` for all except `windows.h`; (b) `static\s+\w+&\s+(Get|Instance)\s*\(` in any header or source under `Source/`; (c) game names (Global Constraints list) under `Source/`, `Examples/`, `Tests/`; (d) a header under `Source/*/include` without `#pragma once`; (e) the strings `Task `, `T1`…`T99` followed by a colon, `fix-round`, `milestone`, `M0`…`M9` followed by a space in a comment under `Source/`. `legacy/` and `third_party/` are skipped.

- [ ] **Step 1: Write `tools/lint.py`** and run it: `py -3 tools/lint.py`. Expected: exit 0 with the W0 tree.
- [ ] **Step 2: Write a lint self-test**: `tools/lint_test.py` creates a temp tree with one violation per rule and asserts each is reported once (`py -3 tools/lint_test.py` → `ok 5`).
- [ ] **Step 3: Write `tools/check.py`** and run `py -3 tools/check.py --preset windows-debug`. Expected: four `ok` lines (configure, build, ctest with "No tests were found" treated as ok until Task 4, lint) and exit 0.
- [ ] **Step 4: Commit** `tools: add check.py as the build verdict and lint.py for layer hygiene`

### Task 4: Test scaffolding, the Standalone header test, CI

**Files:**
- Create: `Tests/CMakeLists.txt`, `Tests/Core/CMakeLists.txt`, `Tests/Core/Main.cpp`, `Tests/Core/Sanity.cpp`, `cmake/StandaloneHeaders.cmake`
- Modify: `.github/workflows/ci.yml` (replace entirely)
- Modify: `CMakeLists.txt` (remove the `if(EXISTS Tests)` guard)

**Interfaces:**
- Produces: `onyx_add_tests(<Layer> SOURCES … LINK …)` in `Tests/CMakeLists.txt`: builds `Onyx<Layer>Tests` from `Tests/<Layer>/Main.cpp` + sources, links `Onyx::<Layer>` + `doctest::doctest` + `LINK`, defines `ONYX_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/Tests/Golden"`, registers one ctest `<Layer>.Unit` running the executable, plus `onyx_add_standalone_headers(<Layer>)`.
- Produces: `onyx_add_standalone_headers(<Layer>)` in `cmake/StandaloneHeaders.cmake`: globs `Source/<Layer>/include/Onyx/<Layer>/**/*.h`, generates one `.cpp` per header in `${CMAKE_BINARY_DIR}/generated/standalone/<Layer>/` containing only `#include <Onyx/<Layer>/…>` and `int OnyxStandalone() { return 0; }`, compiles them as an OBJECT library `Onyx<Layer>Standalone` whose include dirs are **only** `Source/<Layer>/include`, the generated Version dir, and the layer's PUBLIC third-party dirs (taken from `$<TARGET_PROPERTY:Onyx_<Layer>,INTERFACE_INCLUDE_DIRECTORIES>`), and adds it to `ALL`.
- Produces: `Tests/Core/Main.cpp` is `#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN` + include.

- [ ] **Step 1: Write `Tests/Core/Sanity.cpp`**: `TEST_CASE("Core.LayerName") { CHECK(std::string_view(Onyx::Core::LayerName()) == "Core"); }` and `TEST_CASE("Core.Version") { CHECK(ONYX_VERSION_MAJOR == 2); }` (include `<Onyx/Core/Version.h>`).
- [ ] **Step 2: Write the CMake pieces** and run `py -3 tools/check.py`. Expected: `ctest` reports `Core.Unit` passed; `OnyxCoreStandalone` compiled `Layer.h` and `Version.h`.
- [ ] **Step 3: Prove Standalone bites**: temporarily add `#include <imgui.h>` to `Layer.h`, run the build, expect a compile error from the standalone TU, revert.
- [ ] **Step 4: Write `.github/workflows/ci.yml`**: jobs `windows` (windows-latest, `ilammy/msvc-dev-cmd@v1`, `actions/setup-python@v5` 3.12, `py -3 tools/check.py --preset windows-debug`) and `linux` (ubuntu-latest, `apt-get install clang ninja-build`, `python3 tools/check.py --preset linux-debug`); both cache `build/<preset>/_deps` keyed on the hash of `cmake/Deps/*.cmake`. No `continue-on-error` anywhere. (GPU layers compile on both runners; render tests arrive in W2 with their own skip code.)
- [ ] **Step 5: Validate the YAML** with `py -3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/ci.yml'))"` (install `pyyaml` if missing).
- [ ] **Step 6: Commit** `test: add the per-layer test rig, the standalone header check and CI on check.py`

### Task 5: Skeleton documentation and the W0 gate

**Files:**
- Modify: `README.md` (replace with a W0 version), create `CLAUDE.md`, modify `CHANGELOG.md` (prepend an "Unreleased — 2.0.0" section)

- [ ] **Step 1: Write `README.md`** in the AquaKit pattern (spec §15): centred header (`.github/Logo.png` at 200, h1 `OnyxSDK`, bold tagline *"A C++20 SDK for building game-asset tools: formats in, viewers and a renderer out, composed by your host."*, badges platform / C++20 / CMake 3.28+ / Vulkan 1.3), then sections **What it is**, **How it is built** (the nine layers as one paragraph; the SVG figure is a W5 deliverable, leave a one-line placeholder sentence, no image tag), **Status** ("v2 in progress on branch `v2`; `legacy/` is the v1 tree and is not built"), **Build and run** (`py -3 tools/check.py`, the preset list), **Consuming Onyx** (FetchContent snippet linking `Onyx::Core`, the `ONYX_WITH_*` options), **Where to look** (table: design spec, survey, `tools/README.md`, `CLAUDE.md`), footer *Made by JeanxPereira*.
- [ ] **Step 2: Write `CLAUDE.md`** (≤ 60 lines): what Onyx is; the principle (*layers compose; a module writes format knowledge; nothing names a game*); rules numbered 1–10 copied from Global Constraints; tools (`check.py`, `lint.py`, presets, the vcvars note); "Where to look" table.
- [ ] **Step 3: Run `py -3 tools/check.py`**. Expected: exit 0.
- [ ] **Step 4: Commit** `docs: write the v2 skeleton README, CLAUDE.md and changelog entry`

---

## W1 — Core

### Task 6: Foundation types: `Result`, `Log`, `Threading`, `Paths`

**Files:**
- Create: `Source/Core/include/Onyx/Core/Result.h`, `…/Services/Log.h`, `Source/Core/Services/Log.cpp`, `…/Services/Threading.h`, `Source/Core/Services/Threading.cpp`, `…/Services/Paths.h`, `Source/Core/Services/Paths.cpp`, `Source/Core/Pch.h`
- Test: `Tests/Core/Result.cpp`, `Tests/Core/Log.cpp`, `Tests/Core/Threading.cpp`, `Tests/Core/Paths.cpp`
- Reference: `legacy/Source/Services/Logger.cpp`, `legacy/Source/Services/Threading.cpp`, `legacy/Source/Services/PathUtils.cpp`, `legacy/Tests/logger_test.cpp`, `legacy/Tests/threading_test.cpp`, `legacy/Tests/pathutils_test.cpp`

**Interfaces:**
- Produces: `template<class T> class Onyx::Result` with `static Result Ok(T)`, `static Result Fail(Error)`, `bool ok() const`, `T& value()`, `const Error& error() const`, `explicit operator bool`; `Result<void>` specialisation; `struct Onyx::Error { std::string code; std::string message; int32_t native = 0; }`. Monadic helpers are not required.
- Produces: `namespace Onyx::Services { enum class LogLevel { Trace, Debug, Info, Warn, Error }; struct LogEntry { LogLevel level; std::chrono::system_clock::time_point time; std::string category, message; }; class Log { public: static Log& Default(); void SetMinLevel(LogLevel); LogLevel MinLevel() const; using Sink = std::function<void(const LogEntry&)>; uint64_t AddSink(Sink, LogLevel min = LogLevel::Trace); void RemoveSink(uint64_t); void SetRingCapacity(size_t); std::vector<LogEntry> Entries() const; void Clear(); void Write(LogLevel, std::string_view category, std::string_view message); template<class... A> void Write(LogLevel l, std::string_view cat, std::format_string<A...> f, A&&... a) { if (l < MinLevel()) return; Write(l, cat, std::format(f, std::forward<A>(a)...)); } }; uint64_t InstallStderrSink(Log&); uint64_t InstallFileSink(Log&, const std::filesystem::path&, size_t maxBytes = 5u<<20, int rotations = 3); }` and macros `ONYX_LOG_TRACE/DEBUG/INFO/WARN/ERROR(cat, fmt, ...)` writing to `Log::Default()`. The ring is a `std::deque<LogEntry>` with front eviction; the file sink keeps one `std::ofstream` open and rotates by size.
- Produces: `Onyx::Services::MarkMainThread()`, `IsMainThread()`, `ONYX_ASSERT_MAIN_THREAD()` (no-op in `NDEBUG`).
- Produces: `namespace Onyx::Services::Paths { std::filesystem::path Executable(); std::filesystem::path UserConfig(std::string_view appName); std::filesystem::path Normalize(const std::filesystem::path&); bool IsInside(const std::filesystem::path& base, const std::filesystem::path& p); }` — `UserConfig` is `%APPDATA%/<app>` on Windows, `$XDG_CONFIG_HOME/<app>` or `~/.config/<app>` elsewhere; the Windows include is in the `.cpp` only.
- Produces: `Source/Core/Pch.h` with the std headers Core uses.

- [ ] **Step 1: Write the tests.** `Result.cpp`: `Result.OkCarriesValue`, `Result.FailCarriesError`, `Result.VoidOk`. `Log.cpp` (port the legacy cases): `Log.FiltersBelowMinLevelBeforeFormatting` (a sink counter and a formatter side effect via a type whose `std::formatter` increments a counter), `Log.RingEvictsOldestAtCapacity` (capacity 3, write 5, entries are the last 3), `Log.SinkRemovedStopsReceiving`, `Log.FileSinkRotatesBySize` (maxBytes 200, rotations 2, write 100 lines, expect `log`, `log.1`, `log.2` exist and `log.3` does not), `Log.FileSinkKeepsHandleOpen` (write 1000 lines under 50 ms on a temp path — assert elapsed < 500 ms as a regression bound). `Threading.cpp`: `Threading.MainThreadIsMarkedOnce`, `Threading.WorkerIsNotMain`. `Paths.cpp`: `Paths.NormalizeCollapsesDotSegments`, `Paths.IsInsideRejectsTraversal` (`base/../x` → false), `Paths.UserConfigEndsWithAppName`.
- [ ] **Step 2: Run** `py -3 tools/check.py --ctest-regex Core` — expected: build fails on missing headers.
- [ ] **Step 3: Implement** the four headers/sources per Interfaces; add the sources to `Source/Core/CMakeLists.txt`; add the tests to `Tests/Core/CMakeLists.txt`.
- [ ] **Step 4: Run** `py -3 tools/check.py --ctest-regex Core` — expected: PASS, lint ok.
- [ ] **Step 5: Commit** `feat(core): add Result, Log with sinks and ring, main-thread marker and Paths`

### Task 7: Stateless VFS: `IFile`, `Reader`, `MemoryFile`, `OsFile`, `SliceFile`, `IVirtualFileSystem`

**Files:**
- Create: `…/Vfs/IFile.h`, `…/Vfs/Reader.h`, `Source/Core/Vfs/Reader.cpp`, `…/Vfs/MemoryFile.h`, `Source/Core/Vfs/MemoryFile.cpp`, `…/Vfs/OsFile.h`, `Source/Core/Vfs/OsFile.cpp`, `…/Vfs/SliceFile.h`, `Source/Core/Vfs/SliceFile.cpp`, `…/Vfs/IVirtualFileSystem.h`
- Test: `Tests/Core/Vfs.cpp`
- Reference: `legacy/Source/Vfs/OsFile.cpp`, `legacy/Source/Vfs/TransformFile.cpp`, `legacy/Tests/vfs_transformfile_test.cpp`

**Interfaces:**
- Produces: `class IFile { virtual uint64_t Size() const = 0; virtual size_t ReadAt(uint64_t offset, std::span<std::byte> dest) const = 0; }` — `ReadAt` returns bytes read, clamped at `Size()`, 0 past the end; thread-safe for concurrent callers.
- Produces: `class Reader { Reader(std::shared_ptr<const IFile>, uint64_t base = 0, uint64_t size = npos); uint64_t Tell() const; void Seek(uint64_t); uint64_t Remaining() const; bool Eof() const; size_t Read(std::span<std::byte>); template<class T> std::optional<T> Read();  // trivially copyable, little-endian by default
  void SetBigEndian(bool); std::optional<std::string> ReadString(size_t n); std::vector<std::byte> ReadAll(); }` — a short read leaves `Tell()` at the clamp point and returns `nullopt` for typed reads.
- Produces: `MemoryFile(std::vector<std::byte>)`; `static Result<std::shared_ptr<OsFile>> OsFile::Open(const std::filesystem::path&)` (uses `CreateFileW`/`ReadFile` with `OVERLAPPED` offsets on Windows, `pread` elsewhere); `SliceFile(std::shared_ptr<const IFile> parent, uint64_t base, uint64_t size)` clamps `size` to the parent.
- Produces: `struct DirEntry { std::string name; bool isDirectory; uint64_t size; }`; `class IVirtualFileSystem { virtual std::vector<DirEntry> List(std::string_view dir) const = 0; virtual std::shared_ptr<IFile> Open(std::string_view path) const = 0; virtual bool Exists(std::string_view path) const = 0; }`.

- [ ] **Step 1: Write `Tests/Core/Vfs.cpp`**: `Vfs.MemoryFileReadAtClampsAtEnd` (10 bytes, read 8 at offset 6 → returns 4), `Vfs.ReaderTypedReadLittleEndian` (bytes `01 02 00 00` → `Read<uint32_t>() == 0x201`), `Vfs.ReaderBigEndian`, `Vfs.ReaderShortReadReturnsNullopt` (3 bytes left, `Read<uint32_t>()` → nullopt, `Tell()` unchanged), `Vfs.SliceFileClampsToParent` (parent 10, slice base 8 size 100 → `Size()==2`), `Vfs.TwoReadersOverOneFileFromTwoThreads` (two threads each read 10 000 typed values from interleaved regions and verify every value), `Vfs.OsFileOpensNonAsciiPath` (write a temp file named `açãoランス.bin` with `std::ofstream(path)` via `std::filesystem::path` wide ctor, open with `OsFile::Open`, read back), `Vfs.OsFileMissingPathFails` (`!result.ok()` and `error().code == "vfs.open"`).
- [ ] **Step 2: Run** `py -3 tools/check.py --ctest-regex Core` — expected: build fails.
- [ ] **Step 3: Implement** per Interfaces.
- [ ] **Step 4: Run** — expected: PASS.
- [ ] **Step 5: Commit** `feat(core): add a stateless VFS with Reader, OsFile, MemoryFile and SliceFile`

### Task 8: `Lz4` and the synthetic ISO builder in TestKit

**Files:**
- Create: `…/Vfs/Lz4.h`, `Source/Core/Vfs/Lz4.cpp`, `Source/TestKit/include/Onyx/TestKit/IsoBuilder.h`, `Source/TestKit/IsoBuilder.cpp`
- Test: `Tests/Core/Lz4.cpp`, `Tests/TestKit/CMakeLists.txt`, `Tests/TestKit/Main.cpp`, `Tests/TestKit/IsoBuilder.cpp`
- Modify: `Tests/CMakeLists.txt` (add TestKit tests), `Source/TestKit/CMakeLists.txt`

**Interfaces:**
- Produces: `Result<size_t> Onyx::Vfs::Lz4Decompress(std::span<const std::byte> src, std::span<std::byte> dst)` (block format, `LZ4_decompress_safe`), `Result<std::vector<std::byte>> Lz4FrameDecompress(std::span<const std::byte>)` (frame format via `lz4frame`).
- Produces: `class Onyx::TestKit::IsoBuilder { IsoBuilder& AddFile(std::string_view path, std::span<const std::byte>); IsoBuilder& AddDirectory(std::string_view path); std::vector<std::byte> Build(); }` writing a minimal ISO 9660 (PVD at sector 16, 2048-byte sectors, path table, directory records with ISO names uppercased ≤ 31 chars, no Joliet/RR). `Build()` also exposes `struct Corruption { enum Kind { RootSizePastEof, RecordLenBelow34, SelfPointingExtent }; }` through `BuildCorrupt(Corruption::Kind)` for Task 9.

- [ ] **Step 1: Write tests.** `Lz4.cpp`: `Lz4.BlockRoundTrip` (compress with `LZ4_compress_default` in the test, decompress, equal), `Lz4.BlockRejectsTruncated` (`!ok()`), `Lz4.FrameRoundTrip`. `IsoBuilder.cpp`: `TestKit.IsoBuilderWritesPvdAtSector16` (bytes `01 "CD001"` at 16·2048), `TestKit.IsoBuilderIsSectorAligned` (size % 2048 == 0).
- [ ] **Step 2: Run** `py -3 tools/check.py --ctest-regex "Core|TestKit"` — fails.
- [ ] **Step 3: Implement**; link `lz4` PRIVATE in Core.
- [ ] **Step 4: Run** — PASS.
- [ ] **Step 5: Commit** `feat(core): add lz4 decompression and a synthetic ISO builder in TestKit`

### Task 9: `IsoFileSystem` rewritten against `Reader`

**Files:**
- Create: `…/Vfs/IsoFileSystem.h`, `Source/Core/Vfs/IsoFileSystem.cpp`
- Test: `Tests/Core/Iso.cpp`
- Reference: `legacy/Source/Vfs/IsoFileSystem.cpp`, `legacy/Source/Vfs/IsoFile.cpp`

**Interfaces:**
- Produces: `static Result<std::shared_ptr<IsoFileSystem>> IsoFileSystem::Mount(std::shared_ptr<const IFile>)`; implements `IVirtualFileSystem` (`List`, `Open` returning a `SliceFile`, `Exists`); paths are `/`-separated, case-insensitive, version suffix `;1` ignored. Every directory record is bounds-checked against the sector and the file; a record with `length < 34` ends that directory with an Error diag collected in `std::vector<Error> IsoFileSystem::Warnings() const`; directory extents already on the walk stack are skipped (cycle guard); directory recursion depth is capped at 64.

- [ ] **Step 1: Write `Tests/Core/Iso.cpp`** using `IsoBuilder`: `Iso.ListsRootEntries`, `Iso.OpensNestedFileAndReadsBytes`, `Iso.ExistsIsCaseInsensitive`, `Iso.RootSizePastEofReturnsErrorNotHang` (run under a 2 s `std::async` watchdog; `!Mount().ok()` or warnings non-empty, and completes), `Iso.RecordLenBelow34StopsDirectoryWithWarning`, `Iso.SelfPointingExtentDoesNotRecurse`.
- [ ] **Step 2: Run** — fails.
- [ ] **Step 3: Implement** per Interfaces (port the parsing from legacy, replace every unchecked `Read` with `Reader::Read<T>()` + `nullopt` handling).
- [ ] **Step 4: Run** — PASS.
- [ ] **Step 5: Commit** `feat(core): mount ISO 9660 images through the stateless VFS with bounds and cycle guards`

### Task 10: Type identity: `TypeId`, `TypeKey`, `TypeSpec`, `TypeCatalog`, `TypeRegistrar`, `Icons`, `MediaKind`

**Files:**
- Create: `…/Types/TypeId.h`, `…/Types/TypeSpec.h`, `…/Types/TypeCatalog.h`, `Source/Core/Types/TypeCatalog.cpp`, `…/Types/TypeRegistrar.h`, `Source/Core/Types/TypeRegistrar.cpp`, `…/Types/Icons.h`, `…/Domain/MediaKind.h`, `Source/Core/Domain/MediaKind.cpp`
- Test: `Tests/Core/Types.cpp`
- Reference: `legacy/Source/Types/TypeCatalog.cpp`, `legacy/Source/Types/TypeRegistrar.cpp`, `legacy/Tests/types_registry_test.cpp`, `legacy/Tests/typeregistrar_test.cpp`

**Interfaces:**
- Produces: `struct TypeId { uint32_t value = 0; bool Valid() const; <=>; }` + `std::hash`; `struct TypeKey { std::string value; }`; `enum class IconId : uint16_t { None, File, Folder, Image, Mesh, Material, Skeleton, Animation, Audio, Video, Text, Table, Script, Map, Shader, Container, Raw, Palette, Warning, Error, … (≤ 64 entries) }`; `struct Color { float r, g, b, a; }`; `struct TypeSpec { std::string key, label; Domain::MediaKind media = Unknown; IconId icon = IconId::None; Color color{0.6f,0.6f,0.6f,1}; }`.
- Produces: `class TypeCatalog { TypeCatalog(); TypeId Register(std::string_view moduleId, const TypeSpec&); TypeId Find(std::string_view fullKey) const; const TypeSpec& Spec(TypeId) const; Domain::MediaKind Media(TypeId) const; std::string_view Key(TypeId) const; size_t Count() const; std::vector<TypeId> All() const; }` — index 0 is `onyx.unknown`; `Register` of an existing full key returns the existing id and does not overwrite; the catalog is internally mutexed; built-ins `onyx.unknown`, `onyx.folder`, `onyx.raw` are registered by the constructor.
- Produces: `class TypeRegistrar { TypeRegistrar(TypeCatalog&, std::string moduleId); TypeId Add(const TypeSpec&); std::string_view ModuleId() const; }` — the key is stored as `<moduleId>.<spec.key>`; a key containing `.` is rejected (`Add` returns invalid id and the catalog records nothing).
- Produces: `enum class Domain::MediaKind : uint8_t { Unknown, Image, Mesh, Material, Skeleton, Animation, Audio, Video, Text, Table, Script, Map, Shader, Container, Raw }`; `std::string_view Name(MediaKind)`; `IconId DefaultIcon(MediaKind)`.

- [ ] **Step 1: Write `Tests/Core/Types.cpp`**: `Types.CatalogMintsStableHandlesPerKey`, `Types.RegisterExistingKeyReturnsSameId`, `Types.RegistrarPrefixesModuleId` (`Add({"mesh"})` → `Key(id) == "obx.mesh"`), `Types.RegistrarRejectsDottedKey`, `Types.BuiltinsExist` (`Find("onyx.unknown").value == 0`, folder and raw valid), `Types.TwoCatalogsDoNotShareIds` (register the same key in two catalogs; ids may differ and `Spec` of one is not visible in the other), `Types.CatalogRegisterFromTwoThreads` (1000 distinct keys from two threads → `Count() == 1003`), `Types.MediaKindNamesAreUnique`.
- [ ] **Step 2: Run** — fails. **Step 3: Implement.** **Step 4: Run** — PASS.
- [ ] **Step 5: Commit** `feat(core): make the type catalog an object with namespaced keys and an icon set`

### Task 11: Domain data model

**Files:**
- Create: `…/Domain/BoundingBox.h`, `…/Domain/Mesh.h`, `…/Domain/Skeleton.h`, `…/Domain/Animation.h`, `…/Domain/Material.h`, `…/Domain/Texture.h`, `Source/Core/Domain/Texture.cpp`, `…/Domain/Scene.h`, `Source/Core/Domain/Scene.cpp`, `…/Domain/Audio.h`, `…/Domain/Table.h`, `…/Domain/Text.h`, `…/Domain/AssetEntry.h`
- Test: `Tests/Core/Domain.cpp`

**Interfaces:**
- Produces: exactly the structs of spec §5.4 with these additions: `template<class T> struct Key { float time; T value; }`; `enum class PartFlags : uint8_t { None = 0, Sky = 1, Hidden = 2 }` with `|`/`&`; `BoundingBox { glm::vec3 min, max; Center(); Radius(); static BoundingBox Of(std::span<const glm::vec3>); }`; `size_t Domain::BytesPerBlock(PixelFormat)`, `size_t ExpectedByteSize(PixelFormat, uint32_t w, uint32_t h)` (BC1/BC4: 8 bytes per 4×4 block, BC3/BC5/BC7: 16, Rgba8: w·h·4, RgbaF16: w·h·8; block formats round w and h up to multiples of 4); `bool Texture::IsValid() const` = `w>0 && h>0 && pixels.size() == ExpectedByteSize(format,w,h)`; `BoundingBox Scene::ComputeBounds() const` over all parts' positions transformed by `transform`; `struct AssetEntry { std::string name, subtitle; ByteRange source; TypeId type; MediaKind kind; NodeFlags flags; std::vector<AssetEntry> children; }` with `ByteRange { uint32_t fileIndex; uint64_t offset, size; bool Empty() const; }` and `enum class NodeFlags : uint8_t { None, Failed }`.

- [ ] **Step 1: Write `Tests/Core/Domain.cpp`**: `Domain.TextureIsValidRgba8` (2×2 with 16 bytes valid, 15 invalid), `Domain.TextureIsValidBc1RoundsToBlocks` (5×5 BC1 → expects 4 blocks = 32 bytes), `Domain.BoundingBoxOfPoints`, `Domain.SceneComputeBoundsAppliesTransform` (translate by (10,0,0) → min.x == 10), `Domain.PartFlagsCompose`.
- [ ] **Step 2–4: fail → implement → PASS.**
- [ ] **Step 5: Run lint** and confirm zero game-name hits in `Source/Core/include/Onyx/Core/Domain`.
- [ ] **Step 6: Commit** `feat(core): add the game-agnostic domain data model`

### Task 12: Diagnostics, Jobs, EventBus (moved) and Settings (atomic, schema)

**Files:**
- Create: `…/Services/Diagnostics.h`, `Source/Core/Services/Diagnostics.cpp`, `…/Services/Jobs.h`, `Source/Core/Services/Jobs.cpp`, `…/Services/EventBus.h`, `Source/Core/Services/EventBus.cpp`, `…/Services/Settings.h`, `Source/Core/Services/Settings.cpp`, `…/Services/SettingsSchema.h`, `Source/Core/Services/SettingsSchema.cpp`
- Test: `Tests/Core/Diagnostics.cpp`, `Tests/Core/Jobs.cpp`, `Tests/Core/EventBus.cpp`, `Tests/Core/Settings.cpp`
- Reference: `legacy/Source/Services/{Diagnostics,Jobs,EventBus,Settings}.cpp`, `legacy/Tests/{diagnostics,jobs,eventbus,settings}_test.cpp`

**Interfaces:**
- Produces: `Diagnostics.h`, `Jobs.h`, `EventBus.h` with the v1 public API unchanged (the headers are copied, comments stripped to contracts, includes rewritten). One behavioural fix in `Jobs.cpp`: `JobQueue::~JobQueue` must not leave a `JobHandle`'s state `weak_ptr` expired while a worker still holds it — hold the `shared_ptr<JobState>` in the worker loop until `Done` is queued (this is the Linux `weak.expired()` failure).
- Produces: `class Settings { static Settings Load(const std::filesystem::path&); static Settings Empty(); Result<void> Save(); Result<void> SaveIfDirty(); std::optional<bool/int64_t/double/std::string> Get*(std::string_view key) const; void Set(key, value) ×4; bool Dirty() const; const std::filesystem::path& Path() const; std::optional<std::string> LoadError() const; }` — `Save` writes `<path>.tmp` then `std::filesystem::rename` over the target (on Windows via `MoveFileExW` with `MOVEFILE_REPLACE_EXISTING`); `Load` of an unparsable file copies it to `<stem>.broken.toml` once, returns an `Empty()` settings whose `Path()` is the original and whose `LoadError()` is set, and `Save()` on such a settings refuses (`error().code == "settings.broken"`) until `ClearLoadError()` is called.
- Produces: `class SettingsSchema { SettingsSchema& Bool(std::string key, bool def, std::string label = {}); Int(…, int64_t def, int64_t min, int64_t max); Double(…); String(…); Enum(key, std::vector<std::string> options, size_t def); Path(key, def); const std::vector<Entry>& Entries() const; }` + `struct Entry { std::string key, label; Kind kind; std::variant<…> def; … }` and `void ApplyDefaults(const SettingsSchema&, Settings&)` (sets only missing keys).

- [ ] **Step 1: Port the legacy test cases** for diagnostics/jobs/eventbus verbatim (rename files, keep every `TEST_CASE` string prefixed `Diag.`, `Jobs.`, `Events.`), and add `Jobs.HandleOutlivesQueueWithoutExpiredState` (submit a job that sleeps 50 ms, destroy the queue, then `handle.Done()` is true and `handle.Peek()` does not crash). Write `Settings.cpp`: port the legacy cases plus `Settings.SaveIsAtomic` (save, then open the file: no `.tmp` left; content parses), `Settings.BrokenFileIsPreservedAndNotOverwritten` (write `a = [`, Load, expect `<stem>.broken.toml` exists with the same bytes, `LoadError()` set, `Save()` fails, original file bytes unchanged), `Settings.SaveIfDirtyWritesOnlyWhenDirty` (mtime unchanged after a no-op), `Settings.SchemaApplyDefaultsFillsMissingOnly`.
- [ ] **Step 2–4: fail → implement → PASS** (`py -3 tools/check.py --ctest-regex Core`).
- [ ] **Step 5: Commit** `feat(core): move jobs, events and diagnostics; make settings atomic with a declared schema`

### Task 13: Declarative schema

**Files:**
- Create: `…/Schema/Schema.h`, `Source/Core/Schema/Schema.cpp`, `…/Schema/SchemaNode.h`, `Source/Core/Schema/SchemaReader.cpp`, `…/Schema/SchemaJson.h`, `Source/Core/Schema/SchemaJson.cpp`
- Test: `Tests/Core/Schema.cpp`
- Reference: `legacy/Include/Onyx/Schema/{StructDef,DataSchema,DataTypes,NodeInstance}.h`, `legacy/Source/Schema/*.cpp`

**Interfaces:**
- Produces: `enum class FieldType { U8, I8, U16, I16, U32, I32, U64, I64, F32, F64, String, Bytes, Struct, Array, Pad }`; `struct Offset { enum Kind { Next, Absolute, FromStructStart, FromField } kind; uint64_t value; std::string field; }`; `struct Count { enum Kind { Constant, FromField } kind; uint64_t value; std::string field; }`; `class Schema { explicit Schema(std::string name); Schema& Field(std::string name, FieldType, Offset = Offset::Next()); Schema& String(name, size_t len, Offset = Next()); Schema& Bytes(name, size_t len, Offset = Next()); Schema& Pad(size_t); Schema& Struct(name, std::shared_ptr<const Schema>, Offset = Next()); Schema& Array(name, std::shared_ptr<const Schema> element, Count, Offset = Next()); Schema& BigEndian(bool); const std::vector<FieldDef>& Fields() const; }`.
- Produces: `struct SchemaNode { std::string name; FieldType type; std::variant<std::monostate, int64_t, uint64_t, double, std::string, std::vector<std::byte>> value; uint64_t offset = 0, size = 0; std::vector<SchemaNode> children; }`; `Result<SchemaNode> ReadSchema(const Schema&, Vfs::Reader&, uint64_t structStart)`; `std::string ToJson(const SchemaNode&)` (integers printed exactly, `u64` ≥ 2^53 included).

- [ ] **Step 1: Write `Tests/Core/Schema.cpp`**: `Schema.ReadsFixedFieldsInOrder`, `Schema.CountFromEarlierField` (u16 count then array of u32), `Schema.OffsetFromField` (a u32 field holding an absolute offset to a nested struct), `Schema.BigEndianPerSchema`, `Schema.U64KeepsFullPrecision` (`0xFFFFFFFFFFFFFFFF` round-trips to JSON as `18446744073709551615`), `Schema.ShortFileReturnsErrorWithPartialTree` (`!ok()` and `error().message` names the field), `Schema.NestedStructRangeCoversChildren`.
- [ ] **Step 2–4: fail → implement → PASS.**
- [ ] **Step 5: Commit** `feat(core): add the declarative schema reader with exact integers and field-relative layout`

### Task 14: Module contract: `GameModule`, `Probe`, `DecoderRegistry` with `Any`, `Audio`, `Table`, `Schema` and `Capabilities`

**Files:**
- Create: `…/Modules/GameModule.h`, `…/Modules/Probe.h`, `Source/Core/Modules/Probe.cpp`, `…/Modules/DecoderRegistry.h`, `Source/Core/Modules/DecoderRegistry.cpp`, `…/Modules/DecodeContext.h`, `…/Modules/Capabilities.h`
- Test: `Tests/Core/Probe.cpp`, `Tests/Core/Decoders.cpp`
- Reference: `legacy/Include/Onyx/Modules/{GameModule,Probe,DecoderRegistry}.h`, `legacy/Source/Modules/{Probe,DecoderRegistry}.cpp`, `legacy/Tests/{probe,decoderregistry}_test.cpp`

**Interfaces:**
- Produces: `GameModule.h` as spec §5.5 (`ModuleInfo`, `OpenFilter`, `ProbeInput`, `ProbeResult`, `MountSpec`, `ParseResult { bool ok; }`, `IGameModule` with `RegisterSettings(SettingsSchema&)` default no-op). `Probe.h`/`.cpp` moved as v1 (`kProbeFloor = 40`, `ProbeRanking`, `RankProbes` ×2) with `ProbeInput::header` filled from `IFile::ReadAt(0, 64 KiB)`.
- Produces: `struct DecodeContext { struct Document& doc; const Domain::AssetEntry& entry; Vfs::Reader reader /* over entry.source, already positioned */; Services::DiagSink& diags; Services::Progress& progress; Services::Settings& settings; std::shared_ptr<void>& moduleState; }` — `Document` is forward-declared here and defined in Task 15.
- Produces: `using SceneDecoder = std::function<std::unique_ptr<Domain::Scene>(DecodeContext&)>` and likewise `ImageDecoder → Domain::Texture`, `TextDecoder → std::optional<Domain::Text>`, `AudioDecoder → Domain::Audio`, `TableDecoder → Domain::Table`, `AnyDecoder = std::function<std::shared_ptr<void>(DecodeContext&)>`; `class DecoderRegistry { void Scene(TypeId, SceneDecoder); Image; Text; Audio; Table; void Any(TypeId, std::string capability, AnyDecoder); void Schema(TypeId, std::shared_ptr<const Schema::Schema>); Capabilities Of(TypeId) const; std::unique_ptr<Domain::Scene> DecodeScene(DecodeContext&) const; … DecodeImage/Text/Audio/Table; std::shared_ptr<void> DecodeAny(DecodeContext&, std::string_view capability) const; std::shared_ptr<const Schema::Schema> SchemaOf(TypeId) const; void RemoveAll(std::span<const TypeId>); }`; every `Decode*` catches exceptions, reports one `Error` diag `onyx.decode.threw` and returns null.
- Produces: `struct Capabilities { bool scene, image, text, audio, table, schema; std::vector<std::string> any; bool Empty() const; }`.

- [ ] **Step 1: Port `probe_test` and `decoderregistry_test` cases** (prefix `Probe.`, `Decoders.`), and add `Decoders.AnySlotRoundTrips` (register `Any(id, "palette", …)` returning a `shared_ptr<std::vector<int>>`, decode, cast back), `Decoders.CapabilitiesListsEverySlot`, `Decoders.ThrowingAnyDecoderReturnsNullWithOneErrorDiag`, `Decoders.RemoveAllClearsEverySlotForTheIds`, `Decoders.SchemaOfReturnsRegisteredSchema`.
- [ ] **Step 2–4: fail → implement → PASS.**
- [ ] **Step 5: Commit** `feat(core): extend the decoder registry with audio, table, schema and open Any slots behind Capabilities`

### Task 15: `Workspace`, `Document`, `Selection`

**Files:**
- Create: `…/Modules/Workspace.h`, `Source/Core/Modules/Workspace.cpp`, `…/Modules/Document.h`, `…/Modules/Selection.h`, `Source/Core/Modules/Selection.cpp`
- Test: `Tests/Core/Workspace.cpp`, `Tests/Core/Selection.cpp`, `Tests/Core/Support/StubModule.h`
- Reference: `legacy/Source/Modules/Workspace.cpp`, `legacy/Source/Modules/Selection.cpp`, `legacy/Tests/workspace_test.cpp`, `legacy/Tests/selection_test.cpp`

**Interfaces:**
- Produces: `struct WorkspaceOptions { unsigned workerThreads = 2; std::filesystem::path settingsPath; }`; `class Workspace { explicit Workspace(WorkspaceOptions = {}); Result<void> AddModule(std::unique_ptr<IGameModule>); bool RemoveModule(std::string_view id); std::span<IGameModule* const> Modules() const; IGameModule* FindModule(std::string_view idOrHint) const; ProbeRanking Probe(const std::filesystem::path&) const; DocumentId Open(path, std::string_view moduleHint = {}); DocumentId OpenAsync(…); Document* Get(DocumentId); void Close(DocumentId); bool CancelOpen(DocumentId); std::span<const std::shared_ptr<Document>> Documents() const; Services::EventBus& Events(); Services::JobQueue& Jobs(); Services::Settings& Settings(); Types::TypeCatalog& Catalog(); DecoderRegistry& Decoders(); SettingsSchema& ModuleSettings(std::string_view moduleId); void Pump(); /* Jobs().Pump(); Events().Pump(); */ }` — `AddModule` registers types, decoders and settings schema, rejects a duplicate id (`error().code == "workspace.duplicateModule"`), applies the schema's defaults to `Settings()`; `Open` always runs `RankProbes` even with one module and returns 0 below the floor; `RemoveModule` closes that module's documents and calls `Decoders().RemoveAll` for its type ids. Events `DocumentOpened`, `TreeReady`, `DocumentClosed`, `SelectionChanged` as v1.
- Produces: `struct Document { DocumentId id; std::filesystem::path path; IGameModule* module; std::vector<Domain::AssetEntry> roots; std::vector<std::shared_ptr<const Vfs::IFile>> files; /* files[0] is the root */ std::shared_ptr<Vfs::IVirtualFileSystem> mounted; Services::DiagSink diags; std::shared_ptr<void> moduleState; std::atomic<bool> ready; Services::JobHandle parseJob; }`; `struct ContainerContext { Vfs::Reader reader; const std::filesystem::path& path; Services::Settings& settings; Services::DiagSink& diags; Services::Progress& progress; std::shared_ptr<void>& moduleState; std::vector<Domain::AssetEntry>& roots; Vfs::IVirtualFileSystem* mounted; std::vector<std::shared_ptr<const Vfs::IFile>>& files; }`.
- Produces: `Selection.h` as v1 (`NodePath`, `SelectionChanged`, `Resolve`), plus `std::optional<NodePath> FindByName(const Document&, std::string_view name)` (first match, depth-first) used by the CLI.

- [ ] **Step 1: Port the legacy workspace and selection cases** (prefix `Workspace.`, `Selection.`) against a `StubModule` in `Tests/Core/Support/StubModule.h` (configurable probe score, parse that emits N entries, optional throw). Replace the v1 case `A single registered module skips the probe floor entirely` with `Workspace.SingleModuleBelowFloorStillRefuses`. Add `Workspace.RemoveModuleClosesItsDocumentsAndDecoders`, `Workspace.AddModuleAppliesSettingsDefaults`, `Workspace.OpenNonAsciiPath`.
- [ ] **Step 2–4: fail → implement → PASS.**
- [ ] **Step 5: Commit** `feat(core): port the workspace with an owned catalog, always-on probing and module removal`

### Task 16: CLI verb contract in Core and the exporter registry

**Files:**
- Create: `…/Cli/Verb.h`, `…/Cli/Run.h`, `Source/Core/Cli/Run.cpp`, `…/Cli/Exporters.h`, `Source/Core/Cli/Exporters.cpp`, `…/Cli/Json.h`, `Source/Core/Cli/Json.cpp`
- Test: `Tests/Core/CliRun.cpp`

**Interfaces:**
- Produces: `struct Verb { std::string name; std::string usage; std::function<int(Modules::Workspace&, std::span<const std::string_view> args, std::ostream& out, std::ostream& err)> run; }`; `class VerbTable { void Add(Verb); void Add(std::vector<Verb>); const Verb* Find(std::string_view) const; std::vector<std::string_view> Names() const; }`; `int Run(Modules::Workspace&, const VerbTable&, int argc, char** argv, std::ostream& out, std::ostream& err)` — prints usage listing every verb and returns `kUsage` for no/unknown verb; constants `kOk = 0, kUsage = 1, kNoModule = 2, kStrictErrors = 3, kPartial = 4`.
- Produces: `class Exporters { void Add(std::string format, Domain::MediaKind kind, std::function<Result<void>(const void* payload, const std::filesystem::path& out)>); const Exporter* Find(std::string_view format) const; }` owned by `VerbTable` (`table.Exporters()`), so `decode --to <format>` (Task 17) can look up what `Exchange` registered.
- Produces: `std::string Cli::JsonEscape(std::string_view)`; `void PrintDiags(const Services::DiagSink&, std::ostream&)`; the single copy of both.

- [ ] **Step 1: Write `Tests/Core/CliRun.cpp`**: `CliRun.NoArgsPrintsUsageAndReturnsKUsage`, `CliRun.UnknownVerbListsVerbs`, `CliRun.DispatchesByName`, `CliRun.ExportersFindByFormat`, `CliRun.JsonEscapeControlChars`.
- [ ] **Step 2–4: fail → implement → PASS.**
- [ ] **Step 5: Commit** `feat(core): add the CLI verb table, exporter registry and shared JSON helpers`

### Task 17: `Onyx::Cli` stock verbs: `probe`, `list`, `extract`, `decode`

**Files:**
- Create: `Source/Cli/include/Onyx/Cli/StockVerbs.h`, `Source/Cli/Probe.cpp`, `Source/Cli/List.cpp`, `Source/Cli/Extract.cpp`, `Source/Cli/Decode.cpp`, `Source/Cli/Common.h`, `Source/Cli/Common.cpp`
- Test: `Tests/Cli/CMakeLists.txt`, `Tests/Cli/Main.cpp`, `Tests/Cli/Verbs.cpp` (needs OnyxBox from Task 18 — this task's tests use the `StubModule` and a `MemoryFile`-backed stub container; the OnyxBox CLI goldens are Task 18)
- Reference: `legacy/Source/Cli/Commands.cpp`, `legacy/Tests/cli_test.cpp`

**Interfaces:**
- Produces: `std::vector<Cli::Verb> Onyx::Cli::StockVerbs()`. Behaviour per v1 `CmdProbe/CmdList/CmdExtract/CmdDecode` with: `--game <hint>` on every verb; `list --json`; `extract <file> <dir>` refuses names that escape `dir` (`Paths::IsInside`), never allocates from a declared size larger than the file (clamps through `SliceFile`), and returns `kPartial` when any entry failed; `decode <file> <entry|@path> [--strict] [--to <format> --out <path>]` resolves `@0/3/1` as a `NodePath` else by `FindByName`, prints a summary per capability (scene: parts/materials/vertices/triangles; image: w×h format; text: first 80 chars; audio: rate/channels/seconds; table: columns/rows; schema: the JSON; any: the capability names), and `--to` looks up `table.Exporters()`.

- [ ] **Step 1: Port the applicable `cli_test` cases** (every case not about render or OnyxBox bytes) to `Tests/Cli/Verbs.cpp` with prefix `Cli.`, plus `Cli.ExtractReturnsKPartialWhenAnEntryFails`, `Cli.DecodeResolvesNodePathSyntax`, `Cli.ExtractDoesNotAllocateDeclaredSizeBeyondFile` (entry declares 0xFFFFFFFF; run completes under 1 s and writes the clamped bytes).
- [ ] **Step 2–4: fail → implement → PASS** (`--ctest-regex "Core|Cli"`).
- [ ] **Step 5: Commit** `feat(cli): port probe, list, extract and decode onto the verb table`

### Task 18: OnyxBox ported; TestKit goldens and decode smoke; CLI goldens

**Files:**
- Create: `Examples/CMakeLists.txt`, `Examples/OnyxBox/CMakeLists.txt`, `Examples/OnyxBox/include/OnyxBox/Module.h`, `Examples/OnyxBox/Module.cpp`, `Examples/OnyxBox/Format.h`, `Examples/OnyxBox/Writer.h`, `Examples/OnyxBox/Writer.cpp` (the file writer the tests use), `Source/TestKit/include/Onyx/TestKit/Goldens.h`, `Source/TestKit/Goldens.cpp`, `Source/TestKit/include/Onyx/TestKit/DecodeSmoke.h`, `Source/TestKit/DecodeSmoke.cpp`
- Test: `Tests/Core/OnyxBox.cpp`, `Tests/TestKit/Goldens.cpp`, `Tests/Cli/OnyxBoxVerbs.cpp`, `Tests/Golden/obx/*.golden.json` (regenerated from the ported module; the v1 tree goldens under `legacy/Tests` are the comparison source)
- Reference: `legacy/Examples/OnyxBox/*`, `legacy/Source/TestKit/{Goldens,DecodeSmoke}.cpp`, `legacy/Tests/{onyxbox,testkit,mounts}_test.cpp`

**Interfaces:**
- Produces: `class OnyxBox::Module : public Onyx::Modules::IGameModule` with id `obx`, the OBX1/OBP1 formats as in legacy's header, parsing through `ContainerContext::reader`, mesh decoded into `Domain::Scene` with streams (positions/normals/indices), plus a new `kind 4 = table` payload (`u16 cols | u16 rows | rows×cols of u16 len + utf-8`) decoded by `Table`, and `RegisterSettings` declaring `obx.strictToc` (bool, false).
- Produces: `OnyxBox::Writer` (`AddBlob/AddImage/AddText/AddMesh/AddTable`, `std::vector<std::byte> Build()`, `BuildPak(std::vector<std::pair<std::string, std::vector<std::byte>>>)`).
- Produces: `TestKit::SnapshotTree(const Document&) -> std::string` (JSON: name, key, kind, size, flags, children), `CompareTreeGolden(snapshot, goldenPath, diffOut)`, `UpdateGolden(snapshot, goldenPath)` when env `ONYX_UPDATE_GOLDENS=1`; `TestKit::DecodeAll(Workspace&, DocumentId, allowlist) -> SmokeResult` exercising every slot `Capabilities` reports.

- [ ] **Step 1: Port the legacy OnyxBox, TestKit and mounts cases** (prefix `OnyxBox.`, `TestKit.`, `Mounts.`) and the OnyxBox-specific CLI cases from `legacy/Tests/cli_test.cpp` into `Tests/Cli/OnyxBoxVerbs.cpp`; add `OnyxBox.TableEntryDecodes`, `TestKit.DecodeAllCoversAnyAndTableSlots`.
- [ ] **Step 2–4: fail → implement → PASS** (`py -3 tools/check.py`).
- [ ] **Step 5: Commit** `feat(examples): port OnyxBox to v2 with a table payload; port tree goldens and decode smoke`

### Task 19: Core-only consumer gate, standalone enforcement, W1 docs

**Files:**
- Create: `Tests/Consumer/CMakeLists.txt`, `Tests/Consumer/ExternalConsumption.cmake`, `Tests/Consumer/CoreOnly/CMakeLists.txt`, `Tests/Consumer/CoreOnly/Main.cpp`
- Modify: `Tests/CMakeLists.txt`, `README.md` (Status and Consuming sections), `CHANGELOG.md`

**Interfaces:**
- Produces: ctest `Consumer.CoreOnly`: configures `Tests/Consumer/CoreOnly` as a top-level project in a fresh build dir with `FETCHCONTENT_SOURCE_DIR_ONYXSDK=<this repo>` and `-DONYX_WITH_GPU=OFF`, builds it, runs its executable (registers OnyxBox via a copied `Examples/OnyxBox` subdirectory, opens a written `.obx`, prints the tree), and asserts the consumer's `_deps` directory contains none of `volk`, `vulkan`, `vma`, `glslang`, `imgui`, `glfw`, `ffmpeg`.

- [ ] **Step 1: Write the consumer project and the cmake script**; run `py -3 tools/check.py --ctest-regex Consumer`. Expected: PASS.
- [ ] **Step 2: Prove it bites**: temporarily make `Source/Core/CMakeLists.txt` call `onyx_dep_volk()` (write the stub `cmake/Deps/Volk.cmake` from spec §3 pins, it is needed in W2 anyway), run, expect FAIL on the `_deps` assertion, revert the Core change, keep the Deps file.
- [ ] **Step 3: Update `README.md`** Status (W1 done, what works: Core, Cli, TestKit, OnyxBox) and Consuming (Core-only snippet), `CHANGELOG.md` Unreleased with the W1 bullets (one per task, one line each).
- [ ] **Step 4: Run `py -3 tools/check.py`** — exit 0 — and on Linux if available (`linux-debug`); if no Linux machine is at hand, state so in the commit body.
- [ ] **Step 5: Commit** `test: gate Core-only consumption from outside the tree; document W1`

---

## Self-review notes

- Spec §4 (targets, per-component fetch, Standalone, PCH): Tasks 2, 4. §5.1 Vfs: 7, 8, 9. §5.2 types/icons: 10. §5.3 MediaKind: 10. §5.4 domain: 11. §5.5 contract: 14. §5.6 Workspace: 15. §5.7 services: 12. §5.8 settings/log/paths: 6, 12. §5.9 probe: 14. §5.10 schema: 13. §11 Cli: 16, 17. §13 TestKit Core half + OnyxBox: 8, 18. §15 repo/tools/CI/docs: 3, 4, 5, 19. W1 gates (Core tests, CoreOnly, Standalone): 4, 19. Not in this plan by design: Gpu/Render/Ui/Shell/Media (W2–W4), Prism (W4), image compare in TestKit (W2, with the goldens), README SVG (W5).
- Review Focus 1 → Tasks 7, 17; 2 → 12; 3 → 7; 4 → 7, 15; 5 → 14.
- Names used across tasks: `Vfs::Reader`, `Result<T>`, `Services::Settings`, `SettingsSchema`, `Types::TypeCatalog`, `DecoderRegistry::Of`, `Capabilities`, `Modules::Document::files`, `Cli::VerbTable`, `Cli::Exporters`, `TestKit::IsoBuilder`, `OnyxBox::Writer` — each defined once in the task that owns it.
