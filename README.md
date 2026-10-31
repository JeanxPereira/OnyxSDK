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

Onyx is a C++20 SDK for building game-asset tools. A module describes a game's formats; the SDK is meant to supply the virtual filesystem, the viewers, a Vulkan renderer, a CLI and a ready-made shell around it. Version 2 is being rebuilt layer by layer on branch `v2`; the Status section says what exists today.

## How it is built

Onyx is nine static libraries, each with its own `Source/<Layer>/` directory and public `include/` directory, and each linking only the layers below it. When v2 is complete the nine layers are: `Onyx::Core` (holds the virtual filesystem, type identity, domain data, module contract, workspace, jobs, settings and logging), `Onyx::Gpu` (Vulkan ownership), `Onyx::Render` (scene renderer), `Onyx::Ui` (ImGui components drawn inside your own frame), `Onyx::Shell` (the ready-made application), `Onyx::Media` (video and audio), `Onyx::Cli` (stock verbs), `Onyx::Exchange` (glTF, PNG and WAV export) and `Onyx::TestKit` (goldens and synthetic fixtures); the layer contents described here are the plan; Status lists what exists today. A consumer names the layers it uses and links nothing else; there is no aggregate target. The figure of this stack is planned for W5.

## Status

v2 is in progress on branch `v2`. W0 (the skeleton), W1 (Core) and W2 (Gpu and Render) are done. What works today:

- **Core.** The virtual filesystem (OS, memory and slice files, ISO 9660 mounts, lz4), the type catalog, the game-agnostic domain model, the module contract with capability-aware decoders, the workspace that opens documents synchronously or on the job queue, jobs, events and diagnostics, atomic settings with a declared schema, and the declarative schema reader.
- **Cli.** The stock verbs `probe`, `list`, `extract` and `decode`; the verb table itself lives in Core (`Onyx/Core/Cli/Verb.h`).
- **Gpu.** `Onyx::Gpu::Device` either creates its own Vulkan 1.3 device or adopts the host's, with a timeline semaphore and a retire queue that destroys resources once the GPU is past them. Buffers, images and views are RAII handles. `FrameRing` gives each frame in flight a transient ring for per-frame data, `UploadContext` batches uploads into one submit, `PipelineBuilder` builds pipelines over a cached `VkPipelineCache`, and `DescriptorPool`, the MSAA `RenderTarget` and `Swapchain` complete the layer.
- **Render.** `Projection` bakes in the Vulkan flip, `Camera` orbits a bounding box, and `AnimationPlayer` poses a scene from a `Domain::AnimClip`. `SceneRenderer` loads a scene with one upload flush, deduplicates materials, validates skinning, and draws it with optional grid and skeleton overlays; `RenderToImage` does that headlessly into a texture. `Overlays::gizmo` is deferred to W3 with the Ui layer.
- **TestKit.** Tree goldens, the decode smoke check, the synthetic ISO builder, PNG read and write, image comparison with tolerance profiles, and the corpus scene fixtures.
- **OnyxBox.** A synthetic example module (`Onyx::ExampleBox`) with its container writer, used by the tests and by the consumer gate.

The parity gate (`Render.Parity`) renders the frozen corpus and compares it with the goldens; it passed on an AMD Radeon RX 6750 XT within the v1 `[hardware]` tolerances, with no tolerance change. A `[software]` profile is recorded for lavapipe (Mesa 25.2.8 / LLVM 20.1.2); the cause is documented in `Tests/Golden/tolerances.toml`: the AMD GPU exports fp16 colour rounded toward zero where llvmpipe rounds fp32 to nearest, and llvmpipe filters UNORM8 textures in 8-bit fixed point. `Render.Reproducible` checks that two runs are byte-identical, and the Linux `check.py` run is green on WSL with all eleven suites. The oracle tool lives in `Tests/Oracle` (not `Tools/`, because `Tools/` and `tools/` cannot coexist on a case-insensitive filesystem). GPU test suites exit 77 and are reported as skipped when no Vulkan device exists.

`Onyx::Ui`, `Onyx::Shell`, `Onyx::Media` and `Onyx::Exchange` still expose only `LayerName()`. `legacy/` is the v1 tree and is not built. CI is defined for both OSes but has not yet run on a hosted runner.

## Rendering headless

`RenderToImage` creates its own upload context, scene renderer and one-frame ring, renders a scene once and returns the pixels as a `Domain::Texture`, top row first. Given a `Domain::Scene` named `scene` and a `Camera` named `camera`:

```cpp
auto device = Onyx::Gpu::Device::Create({});
if (!device.ok()) { return 1; }   // check ok() before value()
Onyx::Render::View view; view.view = camera.ViewMatrix(); view.proj = camera.Proj(1.0f);
auto image = Onyx::Render::RenderToImage(*device.value(), scene, view, {.grid = true}, {1024, 1024});
```

Both results are `Result` values: check `image.ok()` before reading `image.value()`. To render several scenes with one renderer, use the second overload with a `SceneRenderer`, `Gpu::UploadContext` and `Gpu::FrameRing` you own. `RenderToImage` honours `View::pose`, which poses the scene at a clip and time before the draw; `SceneRenderer::Draw` does not, and reports `render.pose.ignored` when it is set. A host that draws interactively drives an `AnimationPlayer` itself and passes the result to `GpuScene::SetPalette` and `SetWorldTransforms`.

## Embedding in a host

A host that already owns a Vulkan device hands it over with `Gpu::Device::Adopt(ExternalDevice)`: instance, physical device, device, queue, queue family, both proc address loaders and the features it enabled (dynamic rendering, synchronization2, timeline semaphore). Onyx never destroys those handles. The queue is used only through `Queue::Submit` and `Queue::Present` under Onyx's mutex, so the host gives Onyx a dedicated queue of the graphics family or routes its own submissions through `Queue::Submit`. The host's instance and device must be created with API version 1.3 or higher: Onyx calls the core entry points, and `Adopt` refuses with `gpu.feature` naming the first one that does not resolve. Hand-off between the two sides goes through timeline semaphores, in both directions. Onyx to host: `FrameRing::End` returns a `FenceValue` on `Device::GraphicsQueue().Timeline()`; the host's submit waits on that semaphore at that value before it samples `RenderTarget::ResolveView()`, which is left in `SHADER_READ_ONLY_OPTIMAL`. Host to Onyx: before Onyx re-renders a target the host is still sampling, the host signals its own timeline semaphore after the read and passes that semaphore and value to `Frame::AddWait(timeline, value, stage)` between `Begin` and `End`. An adopted device shuts down by waiting on its own timeline, never with `vkDeviceWaitIdle` on the host's device; `Queue::WaitIdle` exists only for swapchain teardown and recreate. Errors carry stable `gpu.*` codes, listed at the top of `Source/Gpu/include/Onyx/Gpu/Device.h`.

## Build and run

```
py -3 tools/check.py                           # configure, build, ctest, lint; exit code is the verdict
py -3 tools/check.py --preset windows-debug    # pick a preset explicitly
```

Presets: `windows-debug`, `windows-release`, `linux-debug`, `linux-release`; build trees go to `build/<preset>`. `check.py` finds the Visual Studio toolchain itself on Windows. See `tools/README.md` for its flags.

## Consuming Onyx

Onyx is consumed from source.

```cmake
include(FetchContent)
FetchContent_Declare(OnyxSDK
    GIT_REPOSITORY https://github.com/JeanxPereira/OnyxSDK.git
    GIT_TAG        v2)   # development branch; 2.0.0 will have a tag
set(ONYX_WITH_GPU OFF)   # only the non-graphical layers
FetchContent_MakeAvailable(OnyxSDK)

target_link_libraries(my_tool PRIVATE Onyx::Core)
```

A Core-only consumer (no GPU, UI or media, and none of their dependencies fetched) sets the options before `FetchContent_MakeAvailable`:

```cmake
set(ONYX_WITH_GPU OFF)
set(ONYX_BUILD_TESTS OFF)
set(ONYX_BUILD_EXAMPLES OFF)
FetchContent_MakeAvailable(OnyxSDK)

target_link_libraries(my_tool PRIVATE Onyx::Core)
```

On Clang with CMake 3.28 or newer, a consumer without `clang-scan-deps` installed configures with `-DCMAKE_CXX_SCAN_FOR_MODULES=OFF` (Onyx uses no C++ modules).

The ctest entry `Consumer.CoreOnly` is the gate for this: it configures `Tests/Consumer/CoreOnly` as a separate top-level project against this checkout with `ONYX_WITH_GPU=OFF`, builds it, runs it, and fails if the consumer's `_deps` holds any volk, Vulkan, VMA, glslang, ImGui, GLFW, ffmpeg or ImPlot directory.

Component options: `ONYX_WITH_GPU` (Gpu and Render), `ONYX_WITH_UI`, `ONYX_WITH_SHELL`, `ONYX_WITH_MEDIA`. Turning a component off turns off those above it. Core, Cli, Exchange and TestKit always build; the shader compiler is `glslang` from the Vulkan SDK when found on the machine, and is fetched otherwise; `-DONYX_WITH_GPU=OFF` drops Gpu, Render, Ui, Shell and Media, leaving only the non-graphical layers (Core, Cli, Exchange, TestKit), and a consumer that links `Onyx::Core` builds and links nothing GPU- or UI-related, and the configure fetches no GPU or UI third-party dependency. `ONYX_BUILD_TESTS`, `ONYX_BUILD_EXAMPLES` and `ONYX_BUILD_TOOLS` default to ON only when Onyx is the top-level project (`ONYX_BUILD_TOOLS` has no consumer yet).

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
