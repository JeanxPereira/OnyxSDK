# Onyx v2 — W2 Gpu + Render Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver `Onyx::Gpu` (Vulkan ownership, embeddable by construction) and `Onyx::Render` (one scene orchestration shared by the viewport and headless output) so the five frozen v1 corpus PNGs render within the v1 tolerances, with the animation, resize-under-validation and submit-count gates green.

**Architecture:** `Gpu` owns or adopts a Vulkan 1.3 device and exposes RAII handles, a retire queue keyed on one timeline semaphore, a frame ring with a per-frame transient buffer, a batched upload context, a pipeline builder backed by a `VkPipelineCache` file, an MSAA render target and a swapchain. `Render` turns a `Domain::Scene` into a `GpuScene` with one upload flush, draws it with one `Draw` used by both the interactive path and `RenderToImage`, samples `AnimClip` keyframes into a joint palette, and draws grid and skeleton overlays. `TestKit` gains the image comparator, PNG I/O, the tolerance file reader and the corpus scene fixtures; `Tests/Oracle` ports the v1 oracle tool and the parity and reproducibility gates.

**Tech Stack:** C++20, CMake 3.28+, Vulkan 1.3 through volk `vulkan-sdk-1.3.296.0` + Vulkan-Headers `v1.3.296` + VMA `v3.1.0`, glslang (installed `glslang`/`glslangValidator` or FetchContent `14.3.0`), glm, stb (`stb_image.h`, `stb_image_write.h`), toml++ (TestKit, private), doctest.

**Spec:** `docs/superpowers/specs/2026-10-30-onyx-v2-design.md` (§3 constraints and tolerances, §4 layers, §6 Gpu, §7 Render, §13 TestKit, §15 repository, §16 W2 gate, §17 builder choices, §18 rulings). Survey: `docs/design/2026-10-30-survey/survey-render.md`. v1 reference: `legacy/Source/Rendering/`, `legacy/Include/Onyx/Rendering/`, `legacy/Tools/OnyxOracle/`, `legacy/Source/App/Window.cpp` (swapchain), `legacy/Include/Onyx/TestKit/RenderCompare.h`.

## Global Constraints

- Branch `v2`. Commit per task with Conventional Commits, imperative Sentence-case subject ≤ 72 chars, explicit paths in `git add`, **no attribution line of any kind**. Never push.
- The verdict is `py -3 tools/check.py --preset windows-debug --jobs 4` (`python3 tools/check.py --preset linux-debug` on Linux). Use `--ctest-regex` to scope while iterating. Never run `check.py` in a tree where another implementer is building.
- Public headers live only under `Source/<Layer>/include/Onyx/<Layer>/`. Namespaces: `Onyx::Gpu`, `Onyx::Render`, `Onyx::TestKit`. Every public header compiles standalone (`OnyxGpuStandalone`, `OnyxRenderStandalone`, `OnyxTestKitStandalone`).
- Layer edges only point down. `Gpu` links `Onyx::Core` plus volk, Vulkan-Headers and VMA (PUBLIC: the raw floor is public). `Render` links `Onyx::Gpu`. `TestKit` links `Onyx::Core` and stb; it never links `Gpu` or `Render`. Forbidden includes per layer are enforced by `tools/lint.py` (Gpu/Render: `imgui`, `GLFW/`, `libav`, `miniaudio`; TestKit: the Core list, which includes `volk`, `vk_mem_alloc`, `vulkan/`).
- No `static X& Get()` / `Instance()` singletons. No process-wide Vulkan state: every object reaches its `Device` by reference. The global volk dispatch table is used only inside `Device` construction; afterwards every device call goes through `Device::Fn()` and every instance call through the per-device `InstanceFns` the device captured.
- Every `VkResult` is checked. A failure returns `Result<T>` with `Error{code, message, native = VkResult}`; never a bool plus a logged string. Error codes used in this plan: `gpu.noDevice`, `gpu.feature`, `gpu.vk`, `gpu.transientFull`, `gpu.extent`, `gpu.surface`, `render.skinning.jointIndex`, `render.skinning.jointMap`, `render.texture.size`, `render.material.index`.
- No `vkQueueWaitIdle`/`vkDeviceWaitIdle` outside `Device::~Device` and `Swapchain::Recreate`. No synchronous submit on the render path; `UploadContext::Flush`, `FrameRing::End` and `Queue::Submit` return a `FenceValue` on the device timeline. The two synchronous helpers (`UploadContext::Download`, `ReadbackColor`) exist for headless output and tests and say so in their header comment.
- All per-frame mapped data (frame UBO, material-independent UBOs, palette SSBO, overlay vertices) is allocated from the frame's transient ring. Nothing is single-buffered.
- No game name (`gow`, `gowr`, `gow1`, `gow2`, `ragnarok`, `wad`, `vag`, `vpk`, `pss`) under `Source/`, `Examples/`, `Tests/`. The v1 shader comment "divergences from GL" and every task-history comment are dropped when porting. English only. PascalCase file and type names; shader sources are `Scene.vert`, `Scene.frag`, `Grid.vert`, `Grid.frag`, `Background.vert`, `Background.frag`, `Overlay.vert`, `Overlay.frag`.
- `legacy/` is read-only reference; nothing includes it.
- Tests use doctest. Suites and ctest names: `Gpu.Unit` (CPU only), `Gpu.Device` (needs a device, SKIP 77), `Render.Unit` (CPU only), `Render.Gpu` (needs a device, SKIP 77), `TestKit.Unit`, `Render.Parity`, `Render.Reproducible` (SKIP 77). A test executable that needs a device probes once in `main` and returns 77 when `Device::Create` fails with `gpu.noDevice`.
- The oracle PNGs in `Tests/Golden/corpus/*.png` are never rewritten by any task. The JSON files beside them are re-blessed once, in Task 9, after the PNG gate passes.
- The machine has one GPU (AMD Radeon RX 6750 XT, the GPU the v1 tolerances were measured on) and WSL with lavapipe. Use `--jobs 4`.

## Review Focus

1. An `ExternalDevice` whose host did not enable `timelineSemaphore`: `Device::Adopt` returns `gpu.feature`, creates nothing, and the host's handles are untouched (Task 2).
2. A `MeshPart` whose `joints` index past its `jointMap`, or a `jointMap` entry past the skeleton: `Load` reports `render.skinning.jointIndex` / `render.skinning.jointMap` and skips the part; no buffer is read out of bounds on a device without `robustBufferAccess` (Task 7).
3. A `Texture` whose `pixels.size()` is not `ExpectedByteSize(format, width, height)`: `Load` reports `render.texture.size`, binds the 1×1 default and never reads past the vector (Task 7).
4. `RenderTarget::Resize` to a zero extent returns `gpu.extent`; a resize while the previous frame still samples the old images retires them through `RetireQueue` instead of destroying them (Task 4).
5. A frame whose overlays exceed the transient ring (the 200-joint skeleton draws 3 200 vertices): `Draw` returns `gpu.transientFull` and the frame still ends cleanly; the default ring size fits the whole corpus (Tasks 3, 8).

---

## Dependency map and lanes

```
Wave A (parallel): T1 deps+shaders [main tree] | T5 TestKit fixtures [lane] | T6 Render CPU [lane] | T10 CI+gate [lane]
Wave B:            T2 Device [main tree]
Wave C (parallel): T3 FrameRing+Upload [lane] | T4 Pipeline+RenderTarget+Swapchain [lane]
Wave D:            T7 SceneRenderer [main tree]
Wave E:            T8 Overlays+RenderToImage+animation gate [main tree]
Wave F:            T9 Oracle+parity [main tree]
Wave G:            T11 lavapipe investigation [WSL]  then  T12 docs [main tree]
```

Sources and tests are globbed; lanes never edit the same CMake file. T1 is the only task that edits `Tests/CMakeLists.txt` and `cmake/`.

---

## Task 1: Vulkan dependencies, shader pipeline and the GPU test rig

**Files:**
- Create: `cmake/Deps/VulkanHeaders.cmake`, `cmake/Deps/Vma.cmake`, `cmake/Deps/Glslang.cmake`, `cmake/Shaders.cmake`, `cmake/GenerateSpirvHeader.cmake`
- Modify: `cmake/Deps/Volk.cmake`, `Source/Gpu/CMakeLists.txt`, `Source/Render/CMakeLists.txt`, `Tests/CMakeLists.txt`, `CMakeLists.txt` (root: `include(cmake/Shaders.cmake)`)
- Create: `Source/Gpu/Pch.h`, `Source/Gpu/VmaImpl.cpp`, `Source/Render/Pch.h`, `Source/Render/Shaders/{Scene,Grid,Background,Overlay}.{vert,frag}` (ported from `legacy/Source/Rendering/Shaders/`, lowercase names → PascalCase, comment blocks that narrate GL, tasks or fixes removed; code unchanged in this task)
- Create: `Tests/Gpu/CMakeLists.txt`, `Tests/Gpu/Main.cpp` (default doctest main for now), `Tests/Gpu/Device/Main.cpp` (default doctest main for now), `Tests/Gpu/Shaders/Triangle.vert`, `Tests/Gpu/Shaders/Triangle.frag`, `Tests/Render/CMakeLists.txt`, `Tests/Render/Main.cpp`, `Tests/Render/Gpu/Main.cpp`
- Modify: `.github/workflows/ci.yml` is **not** touched here (Task 10).

**Interfaces:**
- Produces: `onyx_dep_vulkanheaders()` (FetchContent `KhronosGroup/Vulkan-Headers` tag `v1.3.296`, `VULKAN_HEADERS_ENABLE_MODULE OFF`, tests and install off), `onyx_dep_vma()` (`GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator` tag `v3.1.0`, links `Vulkan::Headers`), `onyx_dep_volk()` reworked: `VOLK_PULL_IN_VULKAN OFF`, `VOLK_INSTALL OFF`, links `Vulkan::Headers`, compile definitions `VK_NO_PROTOTYPES` and `$<$<PLATFORM_ID:Windows>:VK_USE_PLATFORM_WIN32_KHR>` PUBLIC, links `${CMAKE_DL_LIBS}` on non-Windows. All three set function-scope `FETCHCONTENT_UPDATES_DISCONNECTED ON` and are idempotent on `TARGET`.
- Produces: `onyx_dep_glslang()`: `find_program(ONYX_GLSLANG NAMES glslang glslangValidator HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")`; when found, cache `ONYX_GLSLANG` and define no target; otherwise FetchContent `KhronosGroup/glslang` tag `14.3.0` with `ENABLE_OPT OFF`, `GLSLANG_TESTS OFF`, `ENABLE_GLSLANG_BINARIES ON`, and `ONYX_GLSLANG` is `$<TARGET_FILE:glslang-standalone>`.
- Produces: `onyx_add_spirv(<target> <shader>...)` in `cmake/Shaders.cmake`: for each GLSL file (absolute or relative to `CMAKE_CURRENT_SOURCE_DIR`) runs `${ONYX_GLSLANG} --target-env vulkan1.3 -o <stem>.spv <src>` and `cmake -P cmake/GenerateSpirvHeader.cmake` into `${PROJECT_BINARY_DIR}/generated/spirv/<target>/Spirv/<Stem><Stage>.h` declaring `namespace Onyx::Spirv { inline constexpr uint32_t k<Stem><Stage>[] = {…}; inline constexpr size_t k<Stem><Stage>Words = …; }` where `<Stage>` is `Vert` or `Frag` (`Scene.vert` → `SceneVert.h`, `kSceneVert`). The directory is added to `<target>` as a PRIVATE include so sources write `#include <Spirv/SceneVert.h>`. A custom target `<target>_spirv` orders the compile.
- Produces: `onyx_add_tests(<Layer> [SUITE <Name>] [SOURCES …] [LINK …] [SKIP_RETURN_CODE n])`: executable `Onyx<Layer><Suite>Tests` (Suite defaults to `Unit`, in which case the name stays `Onyx<Layer>Tests`), sources default to `Tests/<Layer>/*.cpp` for `Unit` and `Tests/<Layer>/<Suite>/*.cpp` otherwise, ctest name `<Layer>.<Suite>`, `set_tests_properties(… SKIP_RETURN_CODE n)` when given. Existing calls keep working unchanged.
- Produces: `Source/Gpu/CMakeLists.txt` links `PUBLIC Onyx::Core volk::volk Vulkan::Headers GPUOpen::VulkanMemoryAllocator`; `VmaImpl.cpp` defines `VMA_IMPLEMENTATION`, `VMA_STATIC_VULKAN_FUNCTIONS 0`, `VMA_DYNAMIC_VULKAN_FUNCTIONS 1` and includes `<volk.h>` before `<vk_mem_alloc.h>`. `Source/Render/CMakeLists.txt` calls `onyx_add_spirv(Onyx_Render Shaders/Scene.vert … Shaders/Overlay.frag)` (eight files). `Tests/Gpu/CMakeLists.txt` registers `Gpu.Unit` and `Gpu.Device` (SKIP 77) and calls `onyx_add_spirv(OnyxGpuDeviceTests Shaders/Triangle.vert Shaders/Triangle.frag)`; `Tests/Render/CMakeLists.txt` registers `Render.Unit` (LINK `Onyx::TestKit`) and `Render.Gpu` (SKIP 77, LINK `Onyx::TestKit`). `Tests/CMakeLists.txt` adds the two directories under `if(ONYX_WITH_GPU)`.
- Produces: `Source/Gpu/Pch.h` (std set from Core's plus `<volk.h>`, `<vk_mem_alloc.h>`), `Source/Render/Pch.h` (same plus `<glm/glm.hpp>`, `<glm/gtc/matrix_transform.hpp>`, `<glm/gtc/quaternion.hpp>`).

- [ ] **Step 1: Write the five `cmake/` files and rework `Volk.cmake`** as specified. In `GenerateSpirvHeader.cmake` port the hex-to-words loop from `legacy/cmake/GenerateSpirvHeader.cmake`, with the new namespace and names and without the generated-file comment's history.
- [ ] **Step 2: Port the eight shaders** to `Source/Render/Shaders/` with PascalCase names; strip the comment blocks named in the Files list; keep every line of code identical. `Tests/Gpu/Shaders/Triangle.vert` draws a full-screen triangle from `gl_VertexIndex`; `Triangle.frag` writes `vec4(1,0,0,1)`.
- [ ] **Step 3: Wire the layers and the test rig** (`Source/Gpu`, `Source/Render`, `Tests/Gpu`, `Tests/Render`, `Tests/CMakeLists.txt`, `onyx_add_tests` extension). `Tests/Gpu/Main.cpp` and the three other `Main.cpp` are the default doctest main (`DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`) plus one `TEST_CASE("<Suite> rig builds") { CHECK(true); }`.
- [ ] **Step 4: Verify** `py -3 tools/check.py --preset windows-debug --jobs 4 --ctest-regex "Gpu|Render|Consumer"`. Expected: configure fetches `vulkan_headers`, `vma` (and `glslang` only when no `glslang` binary is on PATH or in `VULKAN_SDK`); eight `Spirv/*.h` generated for `Onyx_Render` and two for `OnyxGpuDeviceTests`; `Gpu.Unit`, `Gpu.Device`, `Render.Unit`, `Render.Gpu` pass with the rig test; `Consumer.CoreOnly` still passes (its forbidden list already names `volk vulkan vma glslang`).
- [ ] **Step 5: Verify the component cascade** `cmake --preset windows-debug -B build/coreonly -DONYX_WITH_GPU=OFF` inside `vcvars64` (use `check.py --no-build --no-test` with a temporary preset only if a plain configure is impractical): no `vulkan_headers`, `vma`, `volk`, `glslang` under `build/coreonly/_deps`. Delete `build/coreonly` afterwards.
- [ ] **Step 6: Commit** `build(gpu): fetch Vulkan deps, compile shaders to SPIR-V, add GPU test rig`

## Task 2: Device, queue, timeline, retire queue and RAII handles

**Files:**
- Create: `Source/Gpu/include/Onyx/Gpu/Device.h`, `Source/Gpu/include/Onyx/Gpu/Queue.h`, `Source/Gpu/include/Onyx/Gpu/Retire.h`, `Source/Gpu/include/Onyx/Gpu/Handles.h`, `Source/Gpu/Device.cpp`, `Source/Gpu/Queue.cpp`, `Source/Gpu/Retire.cpp`, `Source/Gpu/Handles.cpp`
- Modify: `Tests/Gpu/Device/Main.cpp` (probe main), delete the rig test case in `Tests/Gpu/Main.cpp`
- Test: `Tests/Gpu/Retire.cpp` (CPU), `Tests/Gpu/Device/Device.cpp`, `Tests/Gpu/Device/Support/TestDevice.h`

**Interfaces:**
- Consumes: `Onyx::Result<T>`, `Onyx::Error` from Core.
- Produces (`Device.h`):
  ```cpp
  namespace Onyx::Gpu {
  using FenceValue = uint64_t;
  struct DeviceOptions {
      bool presentSupport = false;                 // adds VK_KHR_surface + platform surface ext, requires VK_KHR_swapchain
      std::vector<const char*> instanceExtensions; // extra, e.g. from the host's window library
      bool validation = ONYX_DEBUG_DEFAULT;        // true in Debug builds
      std::optional<uint32_t> physicalDeviceIndex; // default: first discrete, else first
      std::filesystem::path pipelineCachePath;     // empty: no cache file
      std::string applicationName = "Onyx";
  };
  struct ExternalFeatures { bool dynamicRendering = false; bool synchronization2 = false; bool timelineSemaphore = false; };
  struct ExternalDevice {
      VkInstance instance; VkPhysicalDevice physicalDevice; VkDevice device; VkQueue queue; uint32_t queueFamily;
      PFN_vkGetInstanceProcAddr getInstanceProcAddr; PFN_vkGetDeviceProcAddr getDeviceProcAddr;
      ExternalFeatures enabled;                    // the host states what it enabled; Adopt refuses any false
      std::filesystem::path pipelineCachePath;
  };
  struct DeviceInfo { std::string deviceName; uint32_t apiVersion; VkPhysicalDeviceType deviceType; bool validation; bool adopted; bool robustBufferAccess; };
  struct InstanceFns { PFN_vkGetPhysicalDeviceProperties2 getPhysicalDeviceProperties2; PFN_vkGetPhysicalDeviceFeatures2 …; PFN_vkGetPhysicalDeviceQueueFamilyProperties …; PFN_vkGetPhysicalDeviceSurfaceSupportKHR …; PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR …; PFN_vkGetPhysicalDeviceSurfaceFormatsKHR …; PFN_vkGetPhysicalDeviceSurfacePresentModesKHR …; PFN_vkDestroySurfaceKHR …; PFN_vkCreateDebugUtilsMessengerEXT …; PFN_vkDestroyDebugUtilsMessengerEXT …; PFN_vkDestroyInstance …; PFN_vkGetPhysicalDeviceFormatProperties …; PFN_vkGetPhysicalDeviceMemoryProperties …; };
  class Device {
  public:
      static Result<std::unique_ptr<Device>> Create(const DeviceOptions&);
      static Result<std::unique_ptr<Device>> Adopt(const ExternalDevice&);
      ~Device();                                   // waits idle, drains Retire(), saves the pipeline cache, destroys owned objects in order
      const VolkDeviceTable& Fn() const;
      const InstanceFns& InstanceFn() const;
      VkInstance Instance() const; VkPhysicalDevice PhysicalDevice() const; VkDevice Handle() const;
      VmaAllocator Allocator() const;
      Queue& GraphicsQueue(); RetireQueue& Retire();
      VkPipelineCache PipelineCache() const;       // VK_NULL_HANDLE never; created empty when no file
      const DeviceInfo& Info() const;
      uint32_t ValidationMessageCount() const; std::string LastValidationMessage() const;
      uint64_t SubmitCount() const;                // incremented by Queue::Submit; the spec's host-queryable counter
  };
  }
  ```
  `Create` enables Vulkan 1.3 features `dynamicRendering`, `synchronization2`, `timelineSemaphore` and `robustBufferAccess` (core feature), refuses a physical device lacking any of the first three with `gpu.feature`, returns `gpu.noDevice` when `vkCreateInstance` fails with `VK_ERROR_INCOMPATIBLE_DRIVER` / `VK_ERROR_INITIALIZATION_FAILED` or no physical device qualifies. Validation: when `options.validation`, enables `VK_LAYER_KHRONOS_validation` if present plus `VK_EXT_debug_utils` and sync validation through `VK_EXT_validation_features` as v1 did (`legacy/Source/Rendering/VkContext.cpp:155-226`); the messenger counts messages and keeps the last text (no logging). `Adopt` validates `enabled` (all three true) and that the physical device supports them, loads a `VolkDeviceTable` with the host's `getDeviceProcAddr`, fills `InstanceFns` with the host's `getInstanceProcAddr`, creates a VMA allocator with `VMA_DYNAMIC_VULKAN_FUNCTIONS`, creates its own timeline semaphore; it never creates a messenger and never destroys the host's handles. `Info().robustBufferAccess` is true for created devices, false for adopted ones.
- Produces (`Queue.h`):
  ```cpp
  struct SemaphoreWait { VkSemaphore semaphore; uint64_t value = 0; VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; };
  struct SemaphoreSignal { VkSemaphore semaphore; uint64_t value = 0; VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; };
  struct SubmitDesc { std::span<const VkCommandBuffer> commandBuffers; std::span<const SemaphoreWait> waits; std::span<const SemaphoreSignal> signals; };
  class Queue {
  public:
      Result<FenceValue> Submit(const SubmitDesc&);   // mutex held; appends the timeline signal value = ++next; increments Device::SubmitCount
      Result<VkResult> Present(const VkPresentInfoKHR&); // same mutex; returns the raw VkResult for the swapchain to interpret
      FenceValue LastSubmitted() const; FenceValue Completed() const;  // vkGetSemaphoreCounterValue
      Result<void> Wait(FenceValue, uint64_t timeoutNs = UINT64_MAX);  // vkWaitSemaphores on the timeline
      VkSemaphore Timeline() const; VkQueue Handle() const; uint32_t Family() const;
  };
  ```
- Produces (`Retire.h`): `class RetireQueue { void Enqueue(std::function<void()> deleter, FenceValue readyAfter); void Collect(FenceValue completed); void Drain(); size_t Pending() const; }` — FIFO, mutex, `Collect` runs every deleter with `readyAfter <= completed`, `Drain` runs all. CPU-testable with no Vulkan.
- Produces (`Handles.h`): `template <class T> class Unique` (move-only; `T` is `VkBuffer`, `VkImage`, `VkImageView`, `VkSampler`, `VkDescriptorPool`, `VkDescriptorSetLayout`, `VkPipelineLayout`, `VkPipeline`, `VkCommandPool`, `VkSemaphore`, `VkShaderModule`; holds `Device*`; destructor enqueues the matching destroy on `Retire()` at `GraphicsQueue().LastSubmitted()`), `struct Buffer { Unique<VkBuffer> handle; VmaAllocation allocation; VkDeviceSize size; void* mapped; }` and `struct Image { Unique<VkImage> handle; VmaAllocation allocation; Unique<VkImageView> view; VkFormat format; VkExtent2D extent; uint32_t mipLevels; VkSampleCountFlagBits samples; }` whose destructors retire the VMA allocation with the handle; `Result<Buffer> CreateBuffer(Device&, VkDeviceSize, VkBufferUsageFlags, MemoryUsage)` with `enum class MemoryUsage { DeviceLocal, HostToDevice, DeviceToHost }` mapped to `VMA_MEMORY_USAGE_AUTO` plus `HOST_ACCESS_SEQUENTIAL_WRITE` / `HOST_ACCESS_RANDOM` and `MAPPED` for the host cases; `Result<Image> CreateImage(Device&, const ImageDesc&)` (`ImageDesc{ extent, format, usage, samples = 1, mipLevels = 1, aspect }`).
- Produces (`Tests/Gpu/Device/Support/TestDevice.h`): `inline std::unique_ptr<Gpu::Device> CreateTestDevice(bool validation = true)` which `REQUIRE`s success. `Tests/Gpu/Device/Main.cpp`: `DOCTEST_CONFIG_IMPLEMENT`, probes `Device::Create({.validation = false})`; on `gpu.noDevice` prints `Gpu.Device: no Vulkan device, skipping` and returns 77; otherwise runs doctest.

- [ ] **Step 1: Write the failing CPU tests** `Tests/Gpu/Retire.cpp`:
  ```cpp
  TEST_CASE("RetireQueue runs deleters in FIFO order once their value completes") {
      Gpu::RetireQueue q; std::vector<int> ran;
      q.Enqueue([&] { ran.push_back(1); }, 3); q.Enqueue([&] { ran.push_back(2); }, 1); q.Enqueue([&] { ran.push_back(3); }, 3);
      q.Collect(2); CHECK(ran == std::vector<int>{2}); CHECK(q.Pending() == 2);
      q.Collect(3); CHECK(ran == std::vector<int>{2, 1, 3});
  }
  TEST_CASE("RetireQueue::Drain runs everything regardless of value") { /* enqueue at 10 and 20, Drain, both ran, Pending()==0 */ }
  TEST_CASE("RetireQueue is safe to Enqueue from a deleter") { /* a deleter enqueues another at the same value; Collect runs the second on the next Collect, no deadlock */ }
  ```
- [ ] **Step 2: Run** `OnyxGpuTests` → FAIL (does not compile). **Implement `Retire.h/.cpp`**, run → PASS.
- [ ] **Step 3: Write the failing device tests** `Tests/Gpu/Device/Device.cpp`:
  ```cpp
  TEST_CASE("Device::Create picks a device, enables the 1.3 features and reports zero validation messages") {
      auto device = CreateTestDevice(); CHECK_FALSE(device->Info().deviceName.empty());
      CHECK(device->Info().apiVersion >= VK_API_VERSION_1_3); CHECK(device->Info().robustBufferAccess);
      CHECK(device->ValidationMessageCount() == 0); CHECK(device->SubmitCount() == 0);
  }
  TEST_CASE("Queue::Submit of an empty submit advances the timeline and the submit counter") {
      auto device = CreateTestDevice(); auto v = device->GraphicsQueue().Submit({}); REQUIRE(v.ok());
      CHECK(v.value() == 1); CHECK(device->GraphicsQueue().Wait(v.value()).ok()); CHECK(device->GraphicsQueue().Completed() >= 1); CHECK(device->SubmitCount() == 1);
  }
  TEST_CASE("Unique handles retire at the last submitted value and Collect destroys them") {
      auto device = CreateTestDevice(); { auto buf = Gpu::CreateBuffer(*device, 256, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, Gpu::MemoryUsage::HostToDevice); REQUIRE(buf.ok()); }
      CHECK(device->Retire().Pending() == 1); auto v = device->GraphicsQueue().Submit({}); device->GraphicsQueue().Wait(v.value());
      device->Retire().Collect(device->GraphicsQueue().Completed()); CHECK(device->Retire().Pending() == 0); CHECK(device->ValidationMessageCount() == 0);
  }
  TEST_CASE("Device::Adopt wraps a host device and refuses a missing feature") {
      auto own = CreateTestDevice();
      Gpu::ExternalDevice ext{ own->Instance(), own->PhysicalDevice(), own->Handle(), own->GraphicsQueue().Handle(), own->GraphicsQueue().Family(), vkGetInstanceProcAddr, vkGetDeviceProcAddr, {true, true, true}, {} };
      auto adopted = Gpu::Device::Adopt(ext); REQUIRE(adopted.ok()); CHECK(adopted.value()->Info().adopted); CHECK_FALSE(adopted.value()->Info().robustBufferAccess);
      auto v = adopted.value()->GraphicsQueue().Submit({}); REQUIRE(v.ok()); CHECK(adopted.value()->GraphicsQueue().Wait(v.value()).ok());
      adopted.value().reset(); CHECK(own->ValidationMessageCount() == 0);   // adopt destroyed nothing of the host's
      ext.enabled.timelineSemaphore = false; auto refused = Gpu::Device::Adopt(ext); REQUIRE_FALSE(refused.ok()); CHECK(refused.error().code == "gpu.feature");
  }
  TEST_CASE("Two owned devices coexist and dispatch through their own tables") { /* two CreateTestDevice(); submit+wait on both; destroy in reverse order; zero validation messages on both */ }
  TEST_CASE("Pipeline cache file is written on destruction and loaded on the next Create") {
      auto path = std::filesystem::temp_directory_path() / "onyx-gpu-cache.bin"; std::filesystem::remove(path);
      { Gpu::DeviceOptions o; o.pipelineCachePath = path; auto d = Gpu::Device::Create(o); REQUIRE(d.ok()); CHECK(d.value()->PipelineCache() != VK_NULL_HANDLE); }
      CHECK(std::filesystem::exists(path)); /* second Create with the same path succeeds and PipelineCache() != VK_NULL_HANDLE */
  }
  ```
- [ ] **Step 4: Run** `OnyxGpuDeviceTests` → FAIL. **Implement `Device`, `Queue`, `Handles`** per the Interfaces; port instance/device selection from `legacy/Source/Rendering/VkContext.cpp:96-393` and the VMA setup from `legacy/Source/Rendering/VmaImpl.cpp`, replacing every `err` string with `Result` and every global with a member. Run → PASS with `ValidationMessageCount() == 0` everywhere.
- [ ] **Step 5: Verify** `py -3 tools/check.py --preset windows-debug --jobs 4 --ctest-regex "Gpu"` green; `OnyxGpuStandalone` compiles every new header.
- [ ] **Step 6: Commit** `feat(gpu): own or adopt a Vulkan 1.3 device with timeline, retire queue and RAII handles`

## Task 3: FrameRing with transient ring, and UploadContext

**Files:**
- Create: `Source/Gpu/include/Onyx/Gpu/FrameRing.h`, `Source/Gpu/include/Onyx/Gpu/Upload.h`, `Source/Gpu/FrameRing.cpp`, `Source/Gpu/Upload.cpp`
- Test: `Tests/Gpu/Device/FrameRing.cpp`, `Tests/Gpu/Device/Upload.cpp`

**Interfaces:**
- Consumes: Task 2 (`Device`, `Queue`, `Buffer`, `Image`, `CreateBuffer`, `CreateImage`), `Domain::Texture`, `Domain::PixelFormat`, `Domain::ExpectedByteSize`.
- Produces (`FrameRing.h`):
  ```cpp
  struct Transient { VkBuffer buffer; VkDeviceSize offset; VkDeviceSize size; void* mapped; };
  class Frame {
  public:
      VkCommandBuffer Cmd() const; uint32_t Index() const;
      Result<Transient> Allocate(VkDeviceSize size, VkDeviceSize alignment);   // bump allocation; gpu.transientFull when exhausted
      void AddWait(FenceValue value);                                           // timeline wait before this frame's commands
      void AddWait(VkSemaphore binary, VkPipelineStageFlags2 stage);           // e.g. swapchain acquire
      void AddSignal(VkSemaphore binary);                                        // e.g. swapchain present
  };
  struct FrameRingOptions { uint32_t framesInFlight = 2; VkDeviceSize transientBytes = 8u << 20; };
  class FrameRing {
  public:
      static Result<std::unique_ptr<FrameRing>> Create(Device&, const FrameRingOptions& = {});
      Result<Frame*> Begin();          // waits this slot's previous FenceValue, Retire().Collect(Completed()), resets pool and ring, begins cmd
      Result<FenceValue> End(Frame&);  // ends cmd, Queue::Submit with the frame's waits/signals, records the value on the slot
      uint32_t FramesInFlight() const; FenceValue LastValue() const;
  };
  ```
  Each slot owns a `VkCommandPool` (`RESET_COMMAND_BUFFER` off; pool reset per frame), one primary command buffer, one host-visible coherent buffer of `transientBytes` with usage `UNIFORM | STORAGE | VERTEX | INDEX | TRANSFER_SRC`, and the slot's last `FenceValue`.
- Produces (`Upload.h`):
  ```cpp
  struct UploadOptions { VkDeviceSize stagingBytes = 64u << 20; };
  class UploadContext {
  public:
      static Result<std::unique_ptr<UploadContext>> Create(Device&, const UploadOptions& = {});
      Result<Buffer> Upload(std::span<const std::byte> bytes, VkBufferUsageFlags usage);   // device-local; records a copy, no submit
      Result<Image> Upload(const Domain::Texture&);   // RGBA8 → R8G8B8A8_UNORM with a full mip chain built by blits; Bc1/3/4/5/7 → BC formats, one mip, no blit; RgbaF16 → R16G16B16A16_SFLOAT; ends in SHADER_READ_ONLY_OPTIMAL
      FenceValue Flush();                             // one Queue::Submit if anything is pending, else returns the last flushed value
      Result<std::vector<std::byte>> Download(const Buffer&, VkDeviceSize size);  // synchronous: Flush, copy to a DeviceToHost buffer, submit, Wait; tests and headless only
      FenceValue LastFlushed() const;
  };
  ```
  Staging is one `HostToDevice` ring of `stagingBytes`; when a request does not fit the remaining space the context flushes, waits for that value, and restarts the ring (documented as the only case where `Flush` count exceeds one per load). `Upload` refuses a texture whose `pixels.size() != ExpectedByteSize(...)` with `render.texture.size`-style code `gpu.upload.size` and never reads past the vector.

- [ ] **Step 1: Write the failing tests**:
  ```cpp
  TEST_CASE("UploadContext uploads bytes and Download returns them after one flush") {
      auto device = CreateTestDevice(); auto up = Gpu::UploadContext::Create(*device); REQUIRE(up.ok());
      std::vector<std::byte> src(4096); /* fill i*7 */ auto buf = up.value()->Upload(src, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT); REQUIRE(buf.ok());
      auto before = device->SubmitCount(); auto v = up.value()->Flush(); CHECK(device->SubmitCount() == before + 1); CHECK(up.value()->Flush() == v);
      auto back = up.value()->Download(buf.value(), 4096); REQUIRE(back.ok()); CHECK(back.value() == src);
  }
  TEST_CASE("Ten buffers and three textures flush in exactly one submit") { /* SubmitCount delta == 1 after Flush; ValidationMessageCount()==0 */ }
  TEST_CASE("UploadContext refuses a texture whose pixel vector is short") { /* 4x4 Rgba8 with 10 bytes → !ok, code "gpu.upload.size" */ }
  TEST_CASE("A block-compressed texture uploads with one mip") { /* 8x8 Bc1 = 32 bytes → ok, mipLevels == 1, format VK_FORMAT_BC1_RGBA_UNORM_BLOCK */ }
  TEST_CASE("Staging exhaustion flushes mid-way instead of failing") { /* stagingBytes = 1 MiB, upload 3 × 512 KiB → all ok, SubmitCount delta >= 2, Download of the third matches */ }
  TEST_CASE("FrameRing reuses a slot only after its submit completed") {
      auto device = CreateTestDevice(); auto ring = Gpu::FrameRing::Create(*device, {.framesInFlight = 2, .transientBytes = 4096}); REQUIRE(ring.ok());
      for (int i = 0; i < 6; ++i) { auto f = ring.value()->Begin(); REQUIRE(f.ok()); CHECK(f.value()->Index() == uint32_t(i % 2)); auto t = f.value()->Allocate(1024, 256); REQUIRE(t.ok()); CHECK(t.value().offset == 0); auto v = ring.value()->End(*f.value()); REQUIRE(v.ok()); CHECK(v.value() == FenceValue(i + 1)); }
      CHECK(device->ValidationMessageCount() == 0);
  }
  TEST_CASE("Frame::Allocate honours alignment and reports exhaustion") { /* 4096 ring: Allocate(100,256) offset 0, Allocate(100,256) offset 256, Allocate(4096,16) → gpu.transientFull; End still ok */ }
  TEST_CASE("Frame waits on an upload value before its commands") { /* up->Flush() value; frame.AddWait(value); End ok; validation 0 */ }
  ```
- [ ] **Step 2: Run** → FAIL. **Implement** `FrameRing` and `UploadContext`; port the image upload path (staging copy, layout transitions with `vkCmdPipelineBarrier2`, mip blits) from `legacy/Source/Rendering/VkResources.cpp:200-342` onto `Device::Fn()`.
- [ ] **Step 3: Run** → PASS. **Verify** `check.py --jobs 4 --ctest-regex "Gpu"` green.
- [ ] **Step 4: Commit** `feat(gpu): frame ring with transient allocations and a batched upload context`

## Task 4: PipelineBuilder with cache, descriptor pool, RenderTarget, Swapchain

**Files:**
- Create: `Source/Gpu/include/Onyx/Gpu/Pipeline.h`, `Source/Gpu/include/Onyx/Gpu/Descriptors.h`, `Source/Gpu/include/Onyx/Gpu/RenderTarget.h`, `Source/Gpu/include/Onyx/Gpu/Swapchain.h`, `Source/Gpu/Pipeline.cpp`, `Source/Gpu/Descriptors.cpp`, `Source/Gpu/RenderTarget.cpp`, `Source/Gpu/Swapchain.cpp`
- Test: `Tests/Gpu/Device/Pipeline.cpp`, `Tests/Gpu/Device/RenderTarget.cpp`, `Tests/Gpu/Device/Swapchain.cpp`

**Interfaces:**
- Consumes: Task 2; Task 3's `Frame`/`FrameRing` are **not** consumed here (the lane runs in parallel): tests use a `VkCommandPool`/`VkCommandBuffer` created through `Device::Fn()` and `Queue::Submit`.
- Produces (`Pipeline.h`):
  ```cpp
  struct VertexBinding { uint32_t binding; uint32_t stride; VkVertexInputRate rate = VK_VERTEX_INPUT_RATE_VERTEX; };
  struct VertexAttribute { uint32_t location; uint32_t binding; VkFormat format; uint32_t offset; };
  struct BlendState { bool enabled = false; VkBlendFactor srcColor, dstColor, srcAlpha, dstAlpha; VkBlendOp colorOp = VK_BLEND_OP_ADD, alphaOp = VK_BLEND_OP_ADD; };
  struct Pipeline { Unique<VkPipeline> pipeline; Unique<VkPipelineLayout> layout; std::vector<Unique<VkDescriptorSetLayout>> setLayouts; };
  class PipelineBuilder {
  public:
      explicit PipelineBuilder(Device&);
      PipelineBuilder& Vertex(std::span<const uint32_t> spirv); PipelineBuilder& Fragment(std::span<const uint32_t> spirv);
      PipelineBuilder& VertexInput(std::span<const VertexBinding>, std::span<const VertexAttribute>);
      PipelineBuilder& Topology(VkPrimitiveTopology);
      PipelineBuilder& SetLayout(std::span<const VkDescriptorSetLayoutBinding>);       // appends set N
      PipelineBuilder& PushConstants(VkShaderStageFlags, uint32_t size);
      PipelineBuilder& Color(VkFormat, const BlendState& = {}); PipelineBuilder& Depth(VkFormat, bool test, bool write, VkCompareOp);
      PipelineBuilder& Samples(VkSampleCountFlagBits); PipelineBuilder& Cull(VkCullModeFlags); PipelineBuilder& Dynamic(std::span<const VkDynamicState>);
      Result<Pipeline> Build();   // vkCreateGraphicsPipelines(device.PipelineCache()); dynamic rendering; viewport+scissor always dynamic
  };
  ```
- Produces (`Descriptors.h`): `class DescriptorPool { static Result<DescriptorPool> Create(Device&, std::span<const VkDescriptorPoolSize>, uint32_t maxSets); Result<VkDescriptorSet> Allocate(VkDescriptorSetLayout); void Reset(); }` (move-only, `Unique<VkDescriptorPool>`), and `Result<Unique<VkSampler>> CreateSampler(Device&, const SamplerDesc&)` with `SamplerDesc{ VkFilter filter = LINEAR; VkSamplerAddressMode address = REPEAT; bool mipmaps = true; float maxAnisotropy = 0; }`.
- Produces (`RenderTarget.h`):
  ```cpp
  struct RenderTargetDesc { VkExtent2D extent; VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM; VkFormat depthFormat = VK_FORMAT_D32_SFLOAT; VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_4_BIT; };
  class RenderTarget {
  public:
      static Result<std::unique_ptr<RenderTarget>> Create(Device&, const RenderTargetDesc&);
      Result<void> Resize(VkExtent2D);   // gpu.extent on zero; old images retire via Unique, never destroyed inline
      void Begin(VkCommandBuffer, const glm::vec4& clear);   // barriers UNDEFINED→ATTACHMENT, vkCmdBeginRendering with MSAA color + depth + resolve (AVERAGE); sets viewport and scissor
      void End(VkCommandBuffer);                              // vkCmdEndRendering; resolve → SHADER_READ_ONLY_OPTIMAL
      VkImageView ResolveView() const; VkImage ResolveImage() const; VkExtent2D Extent() const; const RenderTargetDesc& Desc() const;
      uint64_t Generation() const;      // increments on Resize; a host caches the view keyed on it
  };
  Result<std::vector<std::byte>> ReadbackColor(Device&, RenderTarget&, FenceValue after);   // synchronous: own pool + cmd, copy resolve → DeviceToHost buffer, Submit waiting `after`, Wait; RGBA8 top-down; headless and tests only
  ```
- Produces (`Swapchain.h`):
  ```cpp
  struct SwapchainOptions { VkSurfaceKHR surface; VkExtent2D extent; bool vsync = true; VkFormat preferredFormat = VK_FORMAT_B8G8R8A8_UNORM; };
  struct SwapImage { VkImage image; VkImageView view; uint32_t index; };
  class Swapchain {
  public:
      static Result<std::unique_ptr<Swapchain>> Create(Device&, const SwapchainOptions&);   // gpu.surface when the queue family cannot present
      Result<std::optional<SwapImage>> Acquire(VkSemaphore signal);   // nullopt: OUT_OF_DATE, caller calls Recreate; SUBOPTIMAL returns the image and sets NeedsRecreate()
      Result<void> Present(std::span<const VkSemaphore> wait, uint32_t index);   // OUT_OF_DATE/SUBOPTIMAL set NeedsRecreate(), not an error
      Result<void> Recreate(VkExtent2D);   // the one place besides ~Device that may vkDeviceWaitIdle
      bool NeedsRecreate() const; VkFormat Format() const; VkExtent2D Extent() const; uint32_t ImageCount() const;
  };
  ```
  Port the present path from `legacy/Source/App/Window.cpp:480-620, 999-1182` (surface format choice, FIFO/MAILBOX, image views, every `VkResult` handled).

- [ ] **Step 1: Write the failing tests**:
  ```cpp
  TEST_CASE("PipelineBuilder builds the triangle pipeline against the pipeline cache") {
      auto device = CreateTestDevice(); auto p = Gpu::PipelineBuilder(*device).Vertex(Onyx::Spirv::kTriangleVert).Fragment(Onyx::Spirv::kTriangleFrag).Color(VK_FORMAT_R8G8B8A8_UNORM).Depth(VK_FORMAT_D32_SFLOAT, true, true, VK_COMPARE_OP_LESS).Samples(VK_SAMPLE_COUNT_4_BIT).Build();
      REQUIRE(p.ok()); CHECK(p.value().pipeline.Get() != VK_NULL_HANDLE); CHECK(device->ValidationMessageCount() == 0);
  }
  TEST_CASE("PipelineBuilder reports a shader stage mismatch as gpu.vk, not a crash") { /* fragment only → !ok */ }
  TEST_CASE("RenderTarget clears to the requested colour and reads back top-down") {
      auto device = CreateTestDevice(); auto rt = Gpu::RenderTarget::Create(*device, {.extent = {64, 64}}); REQUIRE(rt.ok());
      /* record Begin(clear {0.25,0.5,0.75,1}) / End in a one-off cmd; Submit; */ auto px = Gpu::ReadbackColor(*device, *rt.value(), v); REQUIRE(px.ok());
      CHECK(px.value().size() == 64 * 64 * 4); /* every pixel == {64,128,191,255} within ±1 */ CHECK(device->ValidationMessageCount() == 0);
  }
  TEST_CASE("Triangle draw covers the target and depth test is active") { /* draw the full-screen triangle; every pixel red */ }
  TEST_CASE("Resize under validation retires the old images and the next frame is clean") {
      /* frame 1: Begin/End + submit (do not wait); rt->Resize({128,96}); CHECK(rt->Generation()==1); frame 2: Begin/End + submit; Wait; CHECK(device->Retire().Pending() > 0); Collect(Completed()); CHECK(device->ValidationMessageCount() == 0); Resize({0,0}) → code gpu.extent */
  }
  TEST_CASE("Swapchain creates against a headless surface when the extension exists") {
      /* if VK_EXT_headless_surface is not an instance extension of the test device → MESSAGE + return (doctest passes vacuously; the suite's Linux run on lavapipe exercises it); else create surface via vkCreateHeadlessSurfaceEXT, Swapchain::Create 256x256, Acquire → image, Present → ok or NeedsRecreate, Recreate(320x240) ok; destroy surface after the swapchain */
  }
  ```
  `CreateTestDevice` must request `presentSupport = true` for the swapchain test; add a `CreateTestDevice(Gpu::DeviceOptions)` overload in `Support/TestDevice.h` (only this task edits that header).
- [ ] **Step 2: Run** → FAIL. **Implement** the four units. `PipelineBuilder::Build` fills `VkPipelineRenderingCreateInfo` from `Color`/`Depth`, uses `VK_DYNAMIC_STATE_VIEWPORT` and `SCISSOR` always. Port the MSAA/resolve/barrier sequence from `legacy/Source/Rendering/OffscreenTarget.cpp:100-240`, dropping the TRANSFER_SRC hop from the interactive path (`ReadbackColor` adds its own SHADER_READ → TRANSFER_SRC → SHADER_READ transitions).
- [ ] **Step 3: Run** → PASS. **Verify** `check.py --jobs 4 --ctest-regex "Gpu"` green, zero validation messages in every test.
- [ ] **Step 4: Commit** `feat(gpu): pipeline builder with cache, descriptor pool, MSAA render target, swapchain`

## Task 5: TestKit image compare, PNG I/O, tolerances and corpus fixtures

**Files:**
- Create: `Source/TestKit/include/Onyx/TestKit/ImageCompare.h`, `Source/TestKit/include/Onyx/TestKit/Png.h`, `Source/TestKit/include/Onyx/TestKit/Tolerances.h`, `Source/TestKit/include/Onyx/TestKit/SceneFixtures.h`, `Source/TestKit/ImageCompare.cpp`, `Source/TestKit/Png.cpp`, `Source/TestKit/Tolerances.cpp`, `Source/TestKit/SceneFixtures.cpp`, `Source/TestKit/FixtureTextures.cpp`, `Tests/Golden/tolerances.toml`, `Tests/Golden/corpus/README.md`
- Modify: `Source/TestKit/CMakeLists.txt` (add `onyx_dep_tomlplusplus()` and `DEPS tomlplusplus::tomlplusplus`), `Source/Core/include/Onyx/Core/Domain/Material.h` (one field, see below)
- Test: `Tests/TestKit/ImageCompare.cpp`, `Tests/TestKit/Png.cpp`, `Tests/TestKit/Tolerances.cpp`, `Tests/TestKit/SceneFixtures.cpp`

**Interfaces:**
- Consumes: `Domain::Scene`, `MeshPart`, `Material`, `Texture`, `Skeleton`, `AnimationSet`, `AnimClip`, `Key<T>`.
- Produces (`Material.h`, Core): add `float vertexColorScale = 1.0f;` after `metallic` with the comment `// multiplies the vertex colour's rgb in the shader; a format that stores colours halved sets 2`. This is the generic knob for the v1 shader's fixed `aColor * 2.0`.
- Produces (`ImageCompare.h`): `inline constexpr int kHighDeltaThreshold = 8;` `struct ImageCompareResult { bool sizeMismatch; int maxChannelDelta; size_t differingPixels, highDeltaPixels, totalPixels; double differingPct, highDeltaPct, mae; }`; `ImageCompareResult CompareRgba(uint32_t width, uint32_t height, std::span<const std::byte> a, std::span<const std::byte> b)` (a size mismatch of the spans sets `sizeMismatch`); `struct CompareTolerance { int maxChannelDelta; double maxDifferingPct, maxHighDeltaPct, maxMae; }`; `bool WithinTolerance(const ImageCompareResult&, const CompareTolerance&)` (all four inclusive); `std::string FormatMetrics(const ImageCompareResult&)` → `maxDelta=%d differingPct=%.4f highDeltaPct=%.4f mae=%.4f`. Semantics are those of `legacy/Include/Onyx/TestKit/RenderCompare.h` (mae = max over channels of the per-channel mean).
- Produces (`Png.h`): `struct Image8 { uint32_t width = 0, height = 0; std::vector<std::byte> rgba; }`; `Result<Image8> ReadPng(const std::filesystem::path&)`; `Result<void> WritePng(const std::filesystem::path&, const Image8&)` (stb, RGBA8, stride `width*4`).
- Produces (`Tolerances.h`): `Result<CompareTolerance> ReadTolerances(const std::filesystem::path&, std::string_view profile)`; `Tests/Golden/tolerances.toml`:
  ```toml
  # The frozen corpus PNGs are the render oracle. A profile is chosen by the device type.
  # hardware: measured on the GPU the v1 parity gate was tuned on (AMD Radeon RX 6750 XT).
  [hardware]
  maxChannelDelta = 160
  maxDifferingPct = 2.0
  maxHighDeltaPct = 0.8
  maxMae = 1.0
  ```
  A missing profile returns an error whose message names the profile and the file.
- Produces (`SceneFixtures.h`):
  ```cpp
  namespace Onyx::TestKit {
  struct SceneFixture { std::string name; Domain::Scene scene; glm::mat4 view; float fovYDegrees = 45.0f; float nearPlane = 0.1f; float farPlane = 100.0f; uint32_t width = 512, height = 512; bool textured = false; };
  std::vector<SceneFixture> BuildCorpus();   // sphere-grid, sphere-grid-textured, skinned-cube, blend-stack, joint-chain-200 in this order
  SceneFixture BuildSphereGrid(); SceneFixture BuildSkinnedCube(); SceneFixture BuildBlendStack(); SceneFixture BuildJointChain200(); SceneFixture BuildAnimatedChain();
  Domain::Texture MakeChecker(uint32_t size, std::array<uint8_t,4> a, std::array<uint8_t,4> b, std::string name);
  Domain::Texture MakeGradient(uint32_t size, std::array<uint8_t,4> top, std::array<uint8_t,4> bottom, std::string name);
  Domain::Texture MakeSolid(uint32_t size, std::array<uint8_t,4> c, std::string name);
  Domain::Texture MakeBumpNormal(uint32_t size, std::string name);
  }
  ```
  Port `legacy/Tools/OnyxOracle/CorpusScenes.cpp` and `CorpusTextures.cpp` onto Domain types with these exact rules, so the PNG gate proves the re-expression: `GpuVertex` streams split into `positions/normals/uv0/uv1/colors/tangents/joints/weights` (colours stay `{1,1,1,1}`; `joints` are `u16vec4`); `MaterialDesc.baseColor` → `baseColor`, `blendColor` → `tint`, `uvOffset`, `metallic`; every material sets `vertexColorScale = 2.0f` and `normalEncoding = Domain::NormalEncoding::ReconstructZ` (the v1 defaults the goldens were rendered with); v1 blend buckets map `Normal` with `textureLayer == 0` → `BlendMode::Opaque`, `Normal` with `textureLayer > 0` → `Alpha`, `Additive` → `Additive`, `Subtractive` → `Subtractive`, `EnvMap` → `EnvMap`; `isSky` → `PartFlags::Sky`; `instanceTransform` → `Scene::transform`; `flipZ` is dropped (every corpus scene sets it false); `view` is the v1 `glm::lookAt` matrix and the projection is expressed as `fovYDegrees/near/far` (the v1 `glm::perspective(radians(45), 1, 0.1, 100)` without the Vulkan flip, which `Render::Projection` applies); `BuildAnimatedChain` is `BuildSkinnedCube` plus an `AnimationSet` with one `AnimClip{ name "bend", duration 1.0 }` whose rotation keys on joints 1 and 2 reproduce the v1 bend (identity at 0 and 1 s, the v1 angle at 0.5 s; read `CorpusScenes.cpp:350-403`). Textures are RGBA8.
- Produces: `Tests/Golden/corpus/README.md` (six lines: the PNGs are the oracle, frozen since v1; the JSON files describe the v2 draw list and are re-blessed by `OnyxOracle bless`; tolerances live in `../tolerances.toml`).

- [ ] **Step 1: Write the failing tests**: port the `OracleCorpus:` cases from `legacy/Tests/oracle_corpus_test.cpp:36-262` that concern generators and scenes (texture generators, determinism, canonical order, sphere-grid 9 parts/materials and 10 textures with all nine roles in range, metallic `{0, 0.5, 1}` by column, skinned-cube weights sum to 1, blend-stack exactly one Additive and one Subtractive, joint-chain-200 200 joints/parts) plus: `TEST_CASE("Every corpus material carries the v1 colour scale and normal encoding")` (`vertexColorScale == 2.0f`, `normalEncoding == ReconstructZ`), `TEST_CASE("BuildAnimatedChain has one 1 s clip with keys on joints 1 and 2")`, and the `CompareRGBA`/`WithinTolerance` cases from `:470-566` renamed to the new API, `TEST_CASE("WritePng then ReadPng round-trips a 3x2 RGBA image")`, `TEST_CASE("ReadTolerances reads the hardware profile and rejects an unknown one")` (reads `ONYX_GOLDEN_DIR "/tolerances.toml"`).
- [ ] **Step 2: Run** `OnyxTestKitTests` → FAIL. **Implement** the five units and the TOML file.
- [ ] **Step 3: Run** → PASS. **Verify** `check.py --jobs 4 --ctest-regex "TestKit|Core"` green (Core still compiles with the new field; `OnyxTestKitStandalone` compiles the new headers).
- [ ] **Step 4: Commit** `feat(testkit): image compare, PNG I/O, tolerance profiles and corpus scene fixtures`

## Task 6: Render CPU side — Projection, Camera, AnimationPlayer, skinning validation

**Files:**
- Create: `Source/Render/include/Onyx/Render/Projection.h`, `Source/Render/include/Onyx/Render/Camera.h`, `Source/Render/include/Onyx/Render/AnimationPlayer.h`, `Source/Render/include/Onyx/Render/Skinning.h`, `Source/Render/include/Onyx/Render/View.h`, `Source/Render/Projection.cpp`, `Source/Render/Camera.cpp`, `Source/Render/AnimationPlayer.cpp`, `Source/Render/Skinning.cpp`
- Test: `Tests/Render/Projection.cpp`, `Tests/Render/Camera.cpp`, `Tests/Render/AnimationPlayer.cpp`, `Tests/Render/Skinning.cpp`

**Interfaces:**
- Consumes: `Domain::Skeleton`, `Joint`, `AnimationSet`, `AnimClip`, `AnimChannel`, `Key<T>`, `MeshPart`, `BoundingBox`, `Services::Diag`.
- Produces (`Projection.h`): `struct Projection { glm::mat4 matrix{1.0f}; static Projection Perspective(float fovYRadians, float aspect, float nearPlane, float farPlane); static Projection Orthographic(float left, float right, float bottom, float top, float nearPlane, float farPlane); }` — both bake the Vulkan clip-space flip (`matrix[1][1]` negated relative to glm) so no caller flips again.
- Produces (`View.h`):
  ```cpp
  enum class ShadingMode : uint8_t { Solid, Textured };
  struct Gradient { glm::vec3 top; glm::vec3 bottom; };
  struct ClipTime { uint32_t clip = 0; float time = 0.0f; };
  struct View { glm::mat4 view{1.0f}; Projection proj; ShadingMode mode = ShadingMode::Solid; glm::vec4 clear{0.10f, 0.11f, 0.13f, 1.0f}; std::optional<Gradient> background; std::optional<ClipTime> pose; };
  struct Overlays { bool grid = false; bool skeleton = false; glm::vec4 gridColor{1,1,1,1}; glm::vec4 boneColor{0.0f, 1.0f, 0.4f, 1.0f}; float gridScale = 1.0f; };
  ```
- Produces (`Camera.h`): the v1 `Camera` ported verbatim from `legacy/Include/Onyx/Rendering/Camera.h` / `Camera.cpp` into `Onyx::Render` with `Projection Proj(float aspect) const` replacing `GetProjectionMatrix` (calls `Projection::Perspective` with the effective planes), and `glm::mat4 ViewMatrix() const`, `glm::mat3 ViewRotation() const`, `FocusOn(const Domain::BoundingBox&)`, `SnapToView(CameraView)`, `UpdateAnimation(float)`; the `AppConfig` read is removed (fields are public; the host syncs them).
- Produces (`Skinning.h`): `std::vector<glm::mat4> RestPalette(const Domain::Skeleton&)` (world(j) = world(parent) * localBind; palette(j) = world(j) * inverseBind); `std::vector<glm::vec3> JointWorldPositions(const Domain::Skeleton&, std::span<const glm::mat4> worldTransforms)`; `std::vector<Services::Diag> ValidateSkinning(const Domain::Scene&)` returning one `Error` diag per offending part: `render.skinning.jointMap` when a `jointMap` entry ≥ `skeleton.joints.size()` (or a part has joints but the scene has no skeleton), `render.skinning.jointIndex` when any `joints[v][k]` with `weights[v][k] > 0` is ≥ `jointMap.size()`, `render.skinning.streams` when `joints.size()`/`weights.size()` differ from `positions.size()` while non-empty; the diag message names the part.
- Produces (`AnimationPlayer.h`):
  ```cpp
  enum class LoopMode : uint8_t { None, Loop, PingPong };
  class AnimationPlayer {
  public:
      void Set(std::shared_ptr<const Domain::Skeleton>, std::shared_ptr<const Domain::AnimationSet>, uint32_t clip);   // bakes lazily; LRU of 8 baked clips keyed by (set pointer, clip)
      void Stop();                           // rest palette, time 0, not playing
      bool Advance(float dt);                // returns true when the palette changed
      void SetTime(float); float Time() const; float Duration() const; void SetPlaying(bool); bool IsPlaying() const; void SetSpeed(float); void SetLoop(LoopMode);
      std::span<const glm::mat4> Palette() const;         // one mat4 per skeleton joint: world * inverseBind
      std::span<const glm::mat4> WorldTransforms() const;  // one per joint, for the skeleton overlay
      bool HasClip() const; uint32_t Clip() const;
  };
  ```
  Sampling: per channel, keys sorted by time; translation and scale lerp, rotation slerp (`glm::slerp`), channels absent keep the joint's `localBind` component; `additive` clips add to the bind pose; baking samples at 60 Hz into `frameCount = ceil(duration * 60) + 1` frames, `Advance` interpolates between baked frames; a clip index past the set leaves `HasClip() == false`.

- [ ] **Step 1: Write the failing tests** (CPU only, `Render.Unit`): `Projection::Perspective(radians(45), 1, 0.1, 100)` has `matrix[1][1] == -glm::perspective(...)[1][1]` and equal other entries; `Camera::FocusOn` of a unit cube places the target at the centre and distance > radius; `RestPalette` of a two-joint chain equals identity when `inverseBind` is the inverse of the world bind; `ValidateSkinning` reports each of the three codes on three malformed parts and nothing on `BuildSkinnedCube().scene`; `AnimationPlayer` on `BuildAnimatedChain()`: `Set` then `Palette()` at t=0 equals the rest palette within 1e-5, `SetTime(0.5)` differs on joints 1 and 2 only, `Advance` past the duration loops in `Loop`, stops at the end in `None`, reverses in `PingPong`, `Stop` restores rest, and nine distinct `(set, clip)` keys evict the first bake (observable through a `BakeCount()` test accessor or re-bake timing-free assertion such as `BakedClips() == 8`).
- [ ] **Step 2: Run** `OnyxRenderTests` → FAIL. **Implement** the four units; the camera is a verbatim port.
- [ ] **Step 3: Run** → PASS. **Verify** `check.py --jobs 4 --ctest-regex "Render.Unit"` green and `OnyxRenderStandalone` compiles every new header.
- [ ] **Step 4: Commit** `feat(render): projection, camera, animation player over AnimClip, skinning validation`

## Task 7: SceneRenderer — Load and Draw

**Files:**
- Create: `Source/Render/include/Onyx/Render/SceneRenderer.h`, `Source/Render/include/Onyx/Render/GpuScene.h`, `Source/Render/SceneRenderer.cpp`, `Source/Render/GpuScene.cpp`, `Source/Render/ScenePipelines.cpp`, `Source/Render/ScenePipelines.h` (private), `Source/Render/ShaderTypes.h` (private: the UBO structs with `static_assert`s)
- Modify: `Source/Render/Shaders/Scene.vert`, `Source/Render/Shaders/Scene.frag` (vertex layout, material UBO, joint map indirection, colour scale), `Tests/Render/Gpu/Main.cpp` (probe main like `Tests/Gpu/Device/Main.cpp`)
- Test: `Tests/Render/Gpu/SceneRenderer.cpp`, `Tests/Render/Gpu/Support/RenderFixture.h`

**Interfaces:**
- Consumes: Tasks 2, 3, 4 (`Device`, `FrameRing`, `Frame`, `UploadContext`, `PipelineBuilder`, `DescriptorPool`, `CreateSampler`, `RenderTarget`, `ReadbackColor`), Task 5 fixtures and compare, Task 6 (`View`, `Overlays`, `Projection`, `RestPalette`, `ValidateSkinning`), `Onyx::Spirv::kSceneVert/kSceneFrag/kBackgroundVert/kBackgroundFrag`.
- Produces (`GpuScene.h`):
  ```cpp
  struct DrawInfo { std::string name; uint32_t materialIndex; uint32_t vertexCount, triangleCount; Domain::BlendMode blend; bool skinned; bool sky; uint32_t texturesBound; uint32_t jointCount; };
  class GpuScene {   // move-only; owns buffers, images, descriptor sets; destructor retires everything
  public:
      std::span<const DrawInfo> Draws() const; Domain::BoundingBox Bounds() const; FenceValue Ready() const;   // the upload flush value
      std::shared_ptr<const Domain::Skeleton> Skeleton() const; std::shared_ptr<const Domain::AnimationSet> Animations() const;
      void SetPalette(std::span<const glm::mat4>);   // CPU copy; size must equal the skeleton's joint count, else ignored with a diag at Draw
      void SetWorldTransforms(std::span<const glm::mat4>);   // for the skeleton overlay (Task 8)
      std::span<const glm::mat4> Palette() const;
  };
  ```
- Produces (`SceneRenderer.h`):
  ```cpp
  struct RendererOptions { VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_4_BIT; VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM; VkFormat depthFormat = VK_FORMAT_D32_SFLOAT; };
  class SceneRenderer {
  public:
      static Result<std::unique_ptr<SceneRenderer>> Create(Gpu::Device&, const RendererOptions& = {});   // builds every pipeline once through PipelineBuilder and the device cache
      Result<GpuScene> Load(const Domain::Scene&, Gpu::UploadContext&, Services::DiagSink* diags = nullptr);   // exactly one Flush
      Result<void> Draw(Gpu::Frame&, Gpu::RenderTarget&, const GpuScene&, const View&, const Overlays&);        // records only; no submit
      const RendererOptions& Options() const;
  };
  ```
  Pipelines: `opaque`, `alpha`, `additive`, `sky` (states ported from `legacy/Source/Rendering/Pipelines.cpp:270-300`: opaque = depth write, LESS, no blend; alpha = no write, LESS_OR_EQUAL, SRC_ALPHA/ONE_MINUS_SRC_ALPHA; additive = no write, LESS_OR_EQUAL, SRC_ALPHA/ONE; sky = as the v1 `sky` variant) and `background`. Cull mode NONE as v1. Pipeline choice: `Opaque` → opaque, `Additive` → additive, `Alpha`/`Subtractive`/`EnvMap` → alpha (the v1 `blendNormal` bucket), `PartFlags::Sky` → sky; `PartFlags::Hidden` parts are not uploaded. Draw order: sky, opaque, blended (in part order), matching v1.
  Vertex layout (interleaved, chosen here): static 48 bytes — `position` `R32G32B32_SFLOAT` @0, `normal` `R16G16B16A16_SNORM` @12 (w 0), `tangent` `R16G16B16A16_SNORM` @20 (w = handedness), `uv0` `R32G32_SFLOAT` @28, `uv1` `R32G32_SFLOAT` @36, `color` `R8G8B8A8_UNORM` @44; skinned 72 bytes — the static 48 then `joints` `R16G16B16A16_UINT` @48, `weights` `R32G32B32A32_SFLOAT` @56. Missing streams fill defaults (normal 0,0,1; tangent 1,0,0,1; uv 0; colour 1,1,1,1). One vertex buffer and one index buffer per scene with per-part ranges. Two scene pipeline variants per blend mode are not needed: unskinned parts bind the 72-byte layout with zero joints and weights (`weights == 0` makes the shader skip skinning) — decision: **one layout, 72 bytes, always**; the 48-byte figure is the spec's estimate and the single layout keeps four pipelines instead of eight. Record this in the task report.
  Materials: deduplicated by hash of `(baseColor, tint, blend, uvOffset, metallic, normalEncoding, vertexColorScale, textures map)`; one material UBO (device-local, 80 bytes: `baseColor vec4, tint vec4, uvOffset vec2, metallic f, flags u32, normalDecode vec4 {2,-1,1,1}, vertexColorScale f, pad vec3`) and one descriptor set (set 1: UBO, seven samplers in the v1 role order Diffuse, Normal, Occlusion, Gloss, EnvMap, Scatter, Opacity, and the part joint-map SSBO at binding 2 is **per part**, so set 1 is per material and set 2 is per part: `{ jointMap SSBO }`) — three sets: 0 frame, 1 material, 2 part. A `textures` entry whose index is past `scene.textures` reports `render.material.index` and binds the 1×1 white default; a texture rejected by `UploadContext::Upload` reports `render.texture.size` and binds the default. Flags mirror v1 `SceneFlags` (`kUseTexture`, `kHasNormal`, `kHasAO`, `kHasGloss`, `kHasScatter`, `kUseJoints`, `kHasEnvmap`, `kHasOpacity`, `kNormalXYZ` for `NormalEncoding::Xyz`).
  Skinning: `Load` runs `ValidateSkinning`; offending parts are reported and skipped. Per part a static u32 SSBO of its `jointMap`; per frame the global palette (`GpuScene::Palette()`, default `RestPalette`) goes into the frame transient ring as an SSBO bound in set 0 binding 1; `Scene.vert` computes `uJoints[uJointMap[aJoints[k]]]`. Set 0 also holds the frame UBO (`view, proj, cameraPos, shadingMode` with Solid = 0, Textured = 2 so the fragment shader's integer semantics stay). The background gradient draws first with the v1 background pipeline from a transient UBO.
  `Draw` records: `target.Begin(cmd, view.clear)`, background if `view.background`, scene passes, `target.End(cmd)`. Overlays are drawn by Task 8's hook between the scene and `End` (leave `DrawOverlays(...)` as a private no-op this task fills in Task 8).
- Produces (`Tests/Render/Gpu/Support/RenderFixture.h`): `struct RenderFixture { std::unique_ptr<Gpu::Device> device; std::unique_ptr<Gpu::UploadContext> upload; std::unique_ptr<Gpu::FrameRing> frames; std::unique_ptr<Render::SceneRenderer> renderer; }`, `RenderFixture MakeRenderFixture()`, `Render::View ViewOf(const TestKit::SceneFixture&)` (lookAt + `Projection::Perspective`, `mode` from `textured`), `Result<TestKit::Image8> RenderOnce(RenderFixture&, const Render::GpuScene&, const View&, const Overlays&, uint32_t w, uint32_t h)` (target, Begin, Draw, End, `ReadbackColor` waiting the frame value).

- [ ] **Step 1: Write the failing tests** (`Render.Gpu`):
  ```cpp
  TEST_CASE("Load flushes exactly once and Draw submits nothing") {
      auto fx = MakeRenderFixture(); auto fixture = TestKit::BuildBlendStack(); auto before = fx.device->SubmitCount();
      auto scene = fx.renderer->Load(fixture.scene, *fx.upload); REQUIRE(scene.ok()); CHECK(fx.device->SubmitCount() == before + 1);
      auto frame = fx.frames->Begin(); auto rt = Gpu::RenderTarget::Create(*fx.device, {.extent = {64, 64}});
      auto mid = fx.device->SubmitCount(); CHECK(fx.renderer->Draw(*frame.value(), *rt.value(), scene.value(), ViewOf(fixture), {}).ok()); CHECK(fx.device->SubmitCount() == mid);
      fx.frames->End(*frame.value()); CHECK(fx.device->SubmitCount() == mid + 1); CHECK(fx.device->ValidationMessageCount() == 0);
  }
  TEST_CASE("Blend-stack renders non-uniform output and is byte-identical across two renders") { /* RenderOnce twice; not all pixels equal the clear; rgba a == rgba b */ }
  TEST_CASE("Sphere-grid textured differs from sphere-grid solid") { /* same scene, mode Solid vs Textured → CompareRgba differingPct > 1 */ }
  TEST_CASE("Materials are deduplicated") { /* a scene with 6 parts sharing 2 identical materials → Draws().size()==6; a MaterialCount() accessor on GpuScene == 2 */ }
  TEST_CASE("Skinned-cube with the rest palette equals skinned-cube with skinning weights zeroed within tolerance") { /* rest pose == bind pose check: palette identity → compare within hardware tolerance */ }
  TEST_CASE("A part with an out-of-range joint index is reported and skipped") { /* BuildSkinnedCube, set joints[0].x = 200 → DiagSink has render.skinning.jointIndex; Draws().size() == parts-1; render ok; validation 0 */ }
  TEST_CASE("A short texture is reported and the default is bound") { /* resize textures[0].pixels to 10 → diag render.texture.size; Load ok; render ok */ }
  TEST_CASE("A material texture index past the pool is reported") { /* textures[Diffuse] = 99 → render.material.index; Load ok */ }
  TEST_CASE("Hidden parts are not uploaded") { /* flag one part Hidden → Draws().size() == parts-1 */ }
  TEST_CASE("Draw into a resized target under validation stays clean") { /* frame 1 draw at 64x64 end; rt->Resize(96x96); frame 2 draw end; wait; validation 0 */ }
  ```
- [ ] **Step 2: Run** `OnyxRenderGpuTests` → FAIL. **Implement** `SceneRenderer`, `GpuScene`, the private pipeline file (each pipeline is one `PipelineBuilder` chain), update the two scene shaders (new attribute formats and locations, `vColor = vec4(aColor.rgb * uMaterial.vertexColorScale, aColor.a)`, joint-map indirection, set 2), port the material/flag resolution from `legacy/Source/Rendering/SceneRendererVk.cpp:330-510` and the frame/draw sequence from `:540-660`. Keep the fragment shader's lighting constants and math untouched.
- [ ] **Step 3: Run** → PASS, zero validation messages. **Verify** `check.py --jobs 4 --ctest-regex "Render|Gpu"` green.
- [ ] **Step 4: Commit** `feat(render): scene renderer with one upload flush, deduplicated materials and skinning`

## Task 8: Overlays, RenderToImage and the animation gate

**Files:**
- Create: `Source/Render/include/Onyx/Render/RenderToImage.h`, `Source/Render/RenderToImage.cpp`, `Source/Render/Overlays.cpp`, `Source/Render/Overlays.h` (private)
- Modify: `Source/Render/SceneRenderer.cpp` (fill `DrawOverlays`), `Source/Render/ScenePipelines.cpp` (grid and overlay pipelines), `Source/Render/Shaders/Overlay.vert` only if the transient layout needs it
- Test: `Tests/Render/Gpu/Overlays.cpp`, `Tests/Render/Gpu/RenderToImage.cpp`, `Tests/Render/Gpu/Animation.cpp`

**Interfaces:**
- Consumes: Task 7, Task 6 `AnimationPlayer`, `JointWorldPositions`, Task 5 fixtures and `CompareRgba`/`ReadTolerances`.
- Produces: grid (`Grid.vert/.frag`, v1 `GridUBO` from the transient ring) and skeleton (`Overlay.vert/.frag`, bone segments and joint markers with the v1 sizes 0.05/0.02/0.04 world units, vertices written into the transient ring each frame; `boneColor`) drawn after the blended pass and before `target.End`. `GpuScene::SetWorldTransforms` feeds the skeleton; the default is the rest world transforms computed at `Load`.
- Produces (`RenderToImage.h`): `struct Extent { uint32_t width, height; }`; `Result<Domain::Texture> RenderToImage(Gpu::Device&, const Domain::Scene&, const View&, const Overlays&, Extent, const RendererOptions& = {})` — creates `UploadContext`, `SceneRenderer`, `FrameRing{1 frame}`, `RenderTarget`; `Load`; when `view.pose` is set runs `AnimationPlayer::Set(skeleton, animations, pose.clip)`, `SetTime(pose.time)` and copies `Palette()`/`WorldTransforms()` into the scene; one `Draw`; `End`; `ReadbackColor`; returns an RGBA8 `Texture` named `"render"`. A second overload `Result<Domain::Texture> RenderToImage(SceneRenderer&, Gpu::UploadContext&, Gpu::FrameRing&, const Domain::Scene&, const View&, const Overlays&, Extent)` reuses a caller's objects (the oracle renders five scenes with one device and one renderer). Both are the only orchestration besides a host's own `Draw` call.

- [ ] **Step 1: Write the failing tests**:
  ```cpp
  TEST_CASE("Grid overlay changes pixels only below the horizon of an empty scene") { /* empty Domain::Scene, RenderToImage with grid off vs on → differingPct > 0; top rows (first 10% of the image) identical */ }
  TEST_CASE("Skeleton overlay draws bones over skinned-cube") { /* overlays.skeleton true vs false → differingPct > 0.1 */ }
  TEST_CASE("RenderToImage renders blend-stack identically to the frame-ring path") { /* RenderOnce(...) vs RenderToImage(device, ...) → CompareRgba maxChannelDelta == 0 */ }
  TEST_CASE("RenderToImage reports a failing scene instead of crashing") { /* Extent{0,0} → !ok, code gpu.extent */ }
  TEST_CASE("Animation gate: rest, set, mid-clip, stopped, resumed") {
      auto tol = TestKit::ReadTolerances(ONYX_GOLDEN_DIR "/tolerances.toml", "hardware"); auto fixture = TestKit::BuildAnimatedChain(); View v = ViewOf(fixture);
      auto a = RenderToImage(device, fixture.scene, v, {}, {512,512});                 // rest
      v.pose = ClipTime{0, 0.0f};  auto b = RenderToImage(...);                           // set, not advanced → within tolerance of a
      v.pose = ClipTime{0, 0.5f};  auto c = RenderToImage(...);                           // mid-clip → outside tolerance of a
      v.pose = std::nullopt;       auto d = RenderToImage(...);                           // stopped → within
      /* and through AnimationPlayer directly on a GpuScene: Set, Advance(0.5), Stop, Advance(1/60) → palette equals rest; render within tolerance */
  }
  ```
- [ ] **Step 2: Run** → FAIL. **Implement** overlays (port `legacy/Source/Rendering/SceneRendererVk.cpp:746-1006` onto the transient ring and the two new pipelines, no lazy pools) and `RenderToImage`.
- [ ] **Step 3: Run** → PASS, zero validation messages. **Verify** `check.py --jobs 4 --ctest-regex "Render|Gpu"` green. Confirm the 200-joint skeleton overlay fits the default transient ring (joint-chain-200 with `skeleton = true` renders ok).
- [ ] **Step 4: Commit** `feat(render): grid and skeleton overlays, RenderToImage, animation gate`

## Task 9: OnyxOracle tool, parity and reproducibility gates, JSON re-bless

**Files:**
- Create: `Tests/Oracle/CMakeLists.txt`, `Tests/Oracle/Main.cpp`, `Tests/Oracle/Commands.h`, `Tests/Oracle/Commands.cpp`, `Tests/Oracle/Report.h`, `Tests/Oracle/Report.cpp`
- Modify: `Tests/CMakeLists.txt` (`add_subdirectory(Oracle)` under `if(ONYX_WITH_GPU)`), `Tests/Golden/corpus/*.json` (re-blessed), `Tests/Golden/corpus/README.md` (one line on `bless`)
- Test: `Tests/Oracle/Report.cpp` cases run through `Tests/Render/Report.cpp`? No: keep the oracle's CPU-testable pieces in `Tests/Oracle/ReportTests.cpp` compiled into a tiny `OnyxOracleTests` (ctest `Oracle.Unit`) that links the same `Report.cpp`.

**Interfaces:**
- Consumes: Task 8 `RenderToImage` (the reusing overload), Task 5 fixtures, `ReadPng/WritePng`, `CompareRgba`, `WithinTolerance`, `ReadTolerances`, `FormatMetrics`, `GpuScene::Draws()`.
- Produces: executable `OnyxOracle` (`Tests/Oracle`, links `Onyx::Render Onyx::TestKit`), commands:
  - `OnyxOracle render --out DIR` renders the five corpus scenes to `DIR/<name>.png` and `DIR/<name>.json`; exit 77 when `Device::Create` fails with `gpu.noDevice`.
  - `OnyxOracle parity --golden DIR --out DIR --tolerances FILE [--profile NAME]` renders, then compares each PNG against the golden with the profile (`--profile` default: `software` when `Info().deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU`, else `hardware`) and prints one `FormatMetrics` line per scene always; exit 0 all within, 1 otherwise, 2 missing file, 3 profile missing from the file (the message names the profile so a lavapipe run without a `[software]` block fails loudly).
  - `OnyxOracle reproducible --out DIR` renders twice into `DIR/a` and `DIR/b`; exit 0 when every PNG is byte-identical.
  - `OnyxOracle bless --golden DIR` renders and writes only the five `.json` files (never a PNG) — refuses with exit 1 if any PNG is missing.
- Produces (`Report.h`): `uint64_t Fnv1a(std::span<const std::byte>)`, `std::string FormatFloat(float)` (six decimals, locale-independent, `-0` normalized), `std::string BuildReport(std::string_view scene, uint32_t w, uint32_t h, uint64_t pixelHash, std::span<const Render::DrawInfo>)` producing
  ```json
  {
    "scene": "<name>", "width": 512, "height": 512, "pixelHash": <u64>,
    "draws": [ { "name": "...", "material": 0, "vertexCount": 0, "triangleCount": 0, "blend": "Opaque|Alpha|Additive|Subtractive|EnvMap", "skinned": false, "sky": false, "texturesBound": 0, "jointCount": 0 } ]
  }
  ```
  with two-space indentation and `\n` line endings, and `bool JsonEqualMaskingPixelHash(std::string_view, std::string_view)`.
- Produces: ctest entries in `Tests/Oracle/CMakeLists.txt`: `Render.Parity` = `OnyxOracle parity --golden ${PROJECT_SOURCE_DIR}/Tests/Golden/corpus --out ${CMAKE_CURRENT_BINARY_DIR}/parity --tolerances ${PROJECT_SOURCE_DIR}/Tests/Golden/tolerances.toml`, `Render.Reproducible` = `OnyxOracle reproducible --out ${CMAKE_CURRENT_BINARY_DIR}/repro`, both `SKIP_RETURN_CODE 77`, labels `render`.

- [ ] **Step 1: Write the failing CPU tests** `Tests/Oracle/ReportTests.cpp`: `Fnv1a` reference vectors (empty → `14695981039346656037`, "a" → `12638187200555641996`), `FormatFloat` six decimals and `-0.000000` → `0.000000` and locale independence under `setlocale(LC_NUMERIC, "de-DE")` when available, `BuildReport` byte-for-byte against a literal for one draw, `JsonEqualMaskingPixelHash` equal/masked/divergent/line-count cases (port from `legacy/Tests/oracle_corpus_test.cpp:264-468`).
- [ ] **Step 2: Implement `Report`**, run `OnyxOracleTests` → PASS.
- [ ] **Step 3: Implement the four commands and the ctest entries.** Run `ctest -R "Render.Parity|Render.Reproducible" --output-on-failure` in `build/windows-debug`. Expected: `Render.Reproducible` passes; `Render.Parity` prints five metric lines and passes within the hardware profile. If a scene fails, do not touch the tolerances: diff the PNGs (write `DIR/<name>.diff.png` with per-pixel |delta|×4 when `--diff` is passed to `parity`), find the cause in the port (blend bucket, colour scale, normal decode, sampler, projection), fix, and re-run. Report every value in the task report.
- [ ] **Step 4: Re-bless the JSON** `OnyxOracle bless --golden Tests/Golden/corpus`; `git diff --stat Tests/Golden/corpus` shows only the five `.json` files changed and no `.png`. Add the `bless` line to the corpus README.
- [ ] **Step 5: Verify** `py -3 tools/check.py --preset windows-debug --jobs 4` fully green.
- [ ] **Step 6: Commit** `test(render): port the oracle tool, parity and reproducibility gates, re-bless JSON`

## Task 10: CI for GPU layers and the consumer gate

**Files:**
- Modify: `.github/workflows/ci.yml`, `Tests/Consumer/ExternalConsumption.cmake` (only if its forbidden list needs `vulkan_headers`; it already matches `vulkan`), `tools/README.md`

**Interfaces:**
- Produces: Linux job installs `clang ninja-build glslang-tools libvulkan-dev mesa-vulkan-drivers vulkan-validationlayers` and runs `check.py` as today (lavapipe provides a CPU device; `Render.Parity` then reads the `software` profile and fails until Task 11 records one or the ruling says otherwise). Windows job unchanged except a note that no Vulkan device exists on hosted runners, so `Gpu.Device`, `Render.Gpu`, `Render.Parity`, `Render.Reproducible` report SKIP (exit 77) there. `tools/README.md` gains a "GPU tests" paragraph: which suites need a device, the exit-77 convention, `glslang` lookup order, how to run the parity gate alone (`check.py --ctest-regex "Render.Parity"`).

- [ ] **Step 1: Edit the three files.** Validate the workflow with `py -3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/ci.yml'))"` (install PyYAML in a venv under the scratchpad if missing, or use `ruby -ryaml`; any parser suffices).
- [ ] **Step 2: Run** `py -3 tools/check.py --preset windows-debug --jobs 4 --ctest-regex Consumer` → green.
- [ ] **Step 3: Commit** `ci: install Vulkan, lavapipe and glslang on Linux; document GPU test suites`

## Task 11: Lavapipe parity investigation

**Files:**
- Modify: `Tests/Golden/tolerances.toml` (a `[software]` block only with a documented cause), or create `docs/design/2026-10-31-lavapipe-parity.md` when no cause is found.

**Interfaces:**
- Consumes: Task 9's `OnyxOracle parity --diff`, WSL (`/mnt/d/CodingProjects/OnyxSDK`, preset `linux-debug`, clang 18, lavapipe at `/usr/lib/x86_64-linux-gnu/libvulkan_lvp.so`).

- [ ] **Step 1: Build on WSL** `python3 tools/check.py --preset linux-debug --jobs 4 --no-test` from `/mnt/d/CodingProjects/OnyxSDK` (memory: four jobs; stop if the machine has under 2 GB free).
- [ ] **Step 2: Run** `ctest --test-dir build/linux-debug -R "Gpu|Render" --output-on-failure`. Record which suites pass on lavapipe. Run `OnyxOracle parity --golden Tests/Golden/corpus --out build/linux-debug/parity --tolerances Tests/Golden/tolerances.toml --profile hardware --diff` and keep the five metric lines and diff images.
- [ ] **Step 3: Investigate** each scene outside the hardware profile with the diff images: classify the differences (MSAA sample positions, anisotropy/mip selection, blend precision, depth precision, a real port error). A real error is fixed in code (commit in this task). An implementation-defined difference with a named cause goes into `tolerances.toml` as
  ```toml
  # software: lavapipe (Mesa llvmpipe Vulkan). Cause: <the finding, one or two sentences>.
  [software]
  ...
  ```
  with the smallest values that pass three consecutive runs. A difference with no identified cause is written up in `docs/design/2026-10-31-lavapipe-parity.md` (per-scene metrics, what was tried, the diff images' description) and **no** `[software]` block is added; the controller rules on the Linux CI outcome.
- [ ] **Step 4: Verify** `python3 tools/check.py --preset linux-debug --jobs 4` on WSL and report its verdict verbatim.
- [ ] **Step 5: Commit** `test(render): record the lavapipe parity profile` or `docs: record the lavapipe parity investigation`

## Task 12: Documentation for W2

**Files:**
- Modify: `README.md` (Status: W0–W2 done; what `Onyx::Gpu` and `Onyx::Render` offer; a "Rendering headless" snippet of `RenderToImage`; the embedding paragraph: `Device::Adopt`, timeline handoff, dedicated queue), `CHANGELOG.md` (W2 section, same style as W1), `CLAUDE.md` (rule 4 unchanged; add to Tools: GPU suites skip with 77 without a device; `Render.Parity` and the tolerance profiles), `docs/superpowers/specs/2026-10-30-onyx-v2-design.md` §15 (`Tools/OnyxOracle` → `Tests/Oracle`, with the reason), `tools/README.md` only if Task 10 left a gap.

- [ ] **Step 1: Write the four edits.** README snippet:
  ```cpp
  auto device = Onyx::Gpu::Device::Create({});
  Onyx::Render::View view; view.view = camera.ViewMatrix(); view.proj = camera.Proj(1.0f);
  auto image = Onyx::Render::RenderToImage(*device.value(), scene, view, {.grid = true}, {1024, 1024});
  ```
- [ ] **Step 2: Run** `py -3 tools/lint.py` → exit 0; `py -3 tools/check.py --preset windows-debug --jobs 4 --lint-only`.
- [ ] **Step 3: Commit** `docs: describe the Gpu and Render layers and the W2 status`

---

## Self-review notes

- Spec coverage: §6 Device own/adopt (T2), handles and retire (T2), frame ring and transient (T3), upload context (T3), pipeline builder + cache (T2 cache object, T4 builder), render target (T4), swapchain (T4), rules (no wait-idle, no synchronous submit on the render path, per-frame data in the ring, robustBufferAccess on created devices and validation on adopted ones, every VkResult checked) are Global Constraints enforced by T2–T8 tests; §7 SceneRenderer one orchestration (T7, T8), projection convention (T6), camera (T6), materials dedupe and vertex layout (T7), skinning validation (T6, T7), animation LRU (T6), four creators → builder (T7); §13 fixtures (T5); §16 W2 gate: goldens (T9), animation gate (T8), resize-under-validation (T4, T7), submit-count (T7); §3 tolerances file and lavapipe (T5, T11).
- Rulings embedded in this plan (the executor records them in the ledger): vertex layout is one 72-byte interleaved layout for every part (§7's 48/72 was an estimate); `Material::vertexColorScale` is added to Core to express the v1 shader's fixed `×2` generically; corpus fixtures set `vertexColorScale = 2` and `ReconstructZ`; the oracle lives in `Tests/Oracle` because `Tools/` and `tools/` cannot coexist on a case-insensitive filesystem; the spec's `Overlays::gizmo` is deferred to W3 (an ImGui-drawn widget in v1); three descriptor sets (frame, material, part); `View::pose` carries the animation time for headless renders; `ReadbackColor` and `UploadContext::Download` are the two synchronous helpers and are confined to headless output and tests; glslang is looked up on the machine before being fetched.
- Type consistency: `FenceValue` (T2) is used by T3, T4, T7, T8; `Image8` (T5) by T7's fixture and T9; `DrawInfo` (T7) by T9; `SceneFixture` (T5) by T7, T8, T9; `View`/`Overlays` (T6) by T7, T8, T9.
