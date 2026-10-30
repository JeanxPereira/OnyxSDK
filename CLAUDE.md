# Onyx

Onyx is a C++20 SDK for game-asset tools, rebuilt as v2 in nine layered static libraries (`Onyx::Core/Gpu/Render/Ui/Shell/Media/Cli/Exchange/TestKit`). Layers compose; a module writes format knowledge; nothing names a game.

## Rules

1. Layers compose and edges only point down. A layer's public headers live only in its own `Source/<Layer>/include/`.
2. A module writes format knowledge; nothing in `Source/` names a game.
3. No singletons behind `Get()`/`Instance()`. The one process-wide object is the default log.
4. No third-party include leaks across layers. Forbidden per layer (enforced by `tools/lint.py`):
   - Core, Cli, Exchange, TestKit: imgui, volk, VMA, vulkan, GLFW, windows.h, libav, miniaudio.
   - Gpu, Render: imgui, GLFW, libav, miniaudio.
   - Ui: GLFW, libav, miniaudio. Shell: libav. Media: GLFW.
5. Every public header compiles standalone (`Tests/<Layer>/Standalone.cpp`).
6. English only. PascalCase files and types. No comment narrates a task, milestone, review or previous version.
7. `tools/check.py` is the verdict: configure, build, ctest, lint, exit code.
8. Commits: Conventional Commits, imperative Sentence-case subject of at most 72 chars, explicit paths, no attribution to AI anywhere. Commit and push only when Jean asks.
9. `legacy/` is reference only: never included, never built, deleted in W5.
10. One worktree per active front. Subagents never touch the same CMake file (sources are globbed).

## Tools

- `py -3 tools/check.py [--preset P]` runs the full verdict. Presets: `windows-debug`, `windows-release`, `linux-debug`, `linux-release` (build dir `build/<preset>`).
- `py -3 tools/lint.py` checks layer hygiene alone.
- On Windows MSVC is not on `PATH`; `check.py` locates Visual Studio through `vswhere` and runs inside `vcvars64.bat` itself. Do not configure by hand from a plain shell.
- Component options: `ONYX_WITH_GPU/UI/SHELL/MEDIA`; turning one off turns off those above it.

## Where to look

| What | Where |
|---|---|
| Design spec | `docs/superpowers/specs/2026-10-30-onyx-v2-design.md` |
| Survey of v1 | `docs/design/2026-10-30-refactor-survey.md` |
| Tools | `tools/README.md` |
| v1 reference | `legacy/README.md` |
