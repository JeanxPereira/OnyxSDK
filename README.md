<p align="center">
  <img src=".github/Logo.png" alt="Onyx" width="200">
  <h1 align="center">OnyxSDK</h1>
  <p align="center">
    <strong>A C++20 SDK for building game-asset tools: formats in, viewers and a renderer out, composed by your host.</strong>
  </p>
  <p align="center">
    <img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux-blue" alt="Platform">
    <img src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus" alt="C++20">
    <img src="https://img.shields.io/badge/CMake-3.28%2B-064F8C?logo=cmake" alt="CMake 3.28+">
    <img src="https://img.shields.io/badge/Vulkan-1.3-AC162C?logo=vulkan" alt="Vulkan 1.3">
  </p>
</p>

---

## What it is

Onyx is a toolkit for applications that open a game's archives, recognise its containers, decode its assets into plain data, and show them in a docked UI, a headless CLI, or a test. A game enters as a module that declares what is particular to it; everything universal belongs to the SDK. You write format knowledge, and Onyx does not name a game.

## How it is built

Onyx is nine static libraries, each with its own `Source/<Layer>/` directory and public `include/` directory, and each linking only the layers below it: `Onyx::Core` (virtual filesystem, type identity, domain data, module contract, workspace, jobs, settings, logging), `Onyx::Gpu` (Vulkan ownership), `Onyx::Render` (scene renderer), `Onyx::Ui` (ImGui components drawn inside your own frame), `Onyx::Shell` (the ready-made application), `Onyx::Media` (video and audio), `Onyx::Cli` (stock verbs), `Onyx::Exchange` (glTF, PNG and WAV export) and `Onyx::TestKit` (goldens and synthetic fixtures). A consumer names the layers it uses and links nothing else; there is no aggregate target. A figure of this stack will be added with the first release.

## Status

v2 is in progress on branch `v2`. W0 (the skeleton) is done: the nine layer targets, the component options, the presets, `tools/check.py` and `tools/lint.py`, the per-layer test rig and CI. `legacy/` is the v1 tree and is not built. Nothing below Core's `LayerName()` exists yet; each layer exposes only that function today.

## Build and run

```
py -3 tools/check.py                         # configure, build, ctest, lint; exit code is the verdict
py -3 tools/check.py --preset linux-release  # pick another preset
```

Presets: `windows-debug`, `windows-release`, `linux-debug`, `linux-release`; build trees go to `build/<preset>`. `check.py` finds the Visual Studio toolchain itself on Windows. See `tools/README.md` for its flags.

## Consuming Onyx

Onyx is consumed from source.

```cmake
include(FetchContent)
FetchContent_Declare(OnyxSDK
    GIT_REPOSITORY https://github.com/JeanxPereira/OnyxSDK.git
    GIT_TAG        v2)
set(ONYX_WITH_GPU OFF)   # Core-only
FetchContent_MakeAvailable(OnyxSDK)

target_link_libraries(my_tool PRIVATE Onyx::Core)
```

Component options: `ONYX_WITH_GPU` (Gpu and Render), `ONYX_WITH_UI`, `ONYX_WITH_SHELL`, `ONYX_WITH_MEDIA`. Turning a component off turns off those above it, so `-DONYX_WITH_GPU=OFF` alone yields a Core-only build. `ONYX_BUILD_TESTS`, `ONYX_BUILD_EXAMPLES` and `ONYX_BUILD_TOOLS` default to ON only when Onyx is the top-level project.

## Where to look

| What | Where |
|---|---|
| Design of v2: layers, contracts, migration plan | [`docs/superpowers/specs/2026-10-30-onyx-v2-design.md`](docs/superpowers/specs/2026-10-30-onyx-v2-design.md) |
| Survey of the v1 tree that motivates it | [`docs/design/2026-10-30-refactor-survey.md`](docs/design/2026-10-30-refactor-survey.md) |
| Build verdict and lint | [`tools/README.md`](tools/README.md) |
| Rules for contributors and agents | [`CLAUDE.md`](CLAUDE.md) |
| The v1 tree, reference only | [`legacy/README.md`](legacy/README.md) |

---

<p align="center">Made by JeanxPereira</p>
