# Onyx

Onyx is a C++20 SDK for game-asset tools, rebuilt as v2 in nine layered static libraries (`Onyx::Core/Gpu/Render/Ui/Shell/Media/Cli/Exchange/TestKit`). Layers compose; a module writes format knowledge; nothing names a game.

## Rules

1. Layers compose and edges only point down. A layer's public headers live only in its own `Source/<Layer>/include/`.
2. A module writes format knowledge; nothing in `Source/` names a game.
3. No singletons behind `Get()`/`Instance()`. The only process-wide object permitted is the default log (`Log::Default()`); nothing else may be reachable without a reference.
4. No third-party include leaks across layers. Forbidden per layer (enforced by `tools/lint.py`):
   - Core, Cli, Exchange, TestKit: imgui, volk, VMA, vulkan, GLFW, windows.h, libav, miniaudio.
   - Gpu, Render: imgui, GLFW, libav, miniaudio.
   - Ui: GLFW, libav, miniaudio. Shell: libav. Media: GLFW.
5. Every public header compiles standalone (`cmake/StandaloneHeaders.cmake`, wired by `onyx_add_tests`).
6. English only. PascalCase files and types. No comment narrates a task, milestone, review or previous version.
7. `tools/check.py` is the verdict: configure, build, ctest, lint, exit code.
8. Commits: Conventional Commits, imperative Sentence-case subject of at most 72 chars, explicit paths, no attribution to AI anywhere. Commit and push only when Jean asks.
9. `legacy/` is reference only: never included, never built, deleted in W5.
10. One worktree per active front. Subagents never touch the same CMake file (sources are globbed).

## Tools

- `py -3 tools/check.py [--preset P]` runs the full verdict. Presets: `windows-debug`, `windows-release`, `linux-debug`, `linux-release` (build dir `build/<preset>`).
- `py -3 tools/lint.py` checks layer hygiene alone.
- On Windows MSVC is not on `PATH`; `check.py` locates Visual Studio through `vswhere` and runs inside `vcvars64.bat` itself. Do not configure by hand from a plain shell.
- GPU test suites (`Gpu.Device`, `Render.Gpu`, `Render.Parity`, `Render.Reproducible`) exit 77 without a Vulkan device and ctest reports them as skipped. `Render.Parity` compares against `Tests/Golden/corpus` with the profile in `Tests/Golden/tolerances.toml` (`[hardware]`, or `[software]` on a CPU device).
- Ui and Shell suites: `Ui.Unit` and `Shell.Unit` need no device; `Ui.Gpu`, `Shell.Window` and `Examples.Embedded` exit 77 without a device (`Shell.Window` also without GLFW or a present-capable device). `Examples.Build` runs `MinimalViewer --help` and needs no device. Set `ONYX_NO_VALIDATION=1` to run the GPU suites where the Khronos validation layer is not installed.
- Other suites: `Exchange.Unit`, `Prism.Unit` and `Examples.Cli` need no device; `Media.Unit` needs FFmpeg at build time and no device; `Media.Gpu` exits 77 without a device. The Media tests open the real audio device when one exists.
- `ONYX_WITH_MEDIA` needs FFmpeg: Windows downloads a pinned build at configure time, Linux needs `libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev pkg-config`; `-DONYX_WITH_MEDIA=OFF` skips it (WSL, for example).
- Component options: `ONYX_WITH_GPU/UI/SHELL/MEDIA`; turning one off turns off those above it.

## Where to look

| What | Where |
|---|---|
| Design spec | `docs/superpowers/specs/2026-10-30-onyx-v2-design.md` |
| Survey of v1 | `docs/design/2026-10-30-refactor-survey.md` |
| Tools | `tools/README.md` |
| v1 reference | `legacy/README.md` |
