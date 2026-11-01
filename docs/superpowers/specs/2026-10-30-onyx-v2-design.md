# Onyx v2 — Design

Onyx is a C++20 SDK for building game-asset tools: applications that open a
game's archives, recognise its containers, decode its assets into plain data,
and show them in a docked UI, a headless CLI, or a test. A game enters as a
**module** that declares what is particular to it. Everything universal is the
SDK's.

v2 replaces v1. It is a break-and-port, not a migration: the public surface is
designed clean, the v1 consumer (GoWToolkit) ports to it afterwards, and the
v1.x goldens are the oracle that proves nothing regressed. The survey that
motivates every decision here is
`docs/design/2026-10-30-refactor-survey.md`; its appendices carry the
evidence.

---

## 1. Intent

**The v1 promise** ("a new toolkit writes format knowledge; no UI, render or
CLI code") held for formats and the CLI and failed for UI. Two of three real
consumers ejected the shell within weeks; the only one that stayed is the
author's own. The renderer cannot be embedded, the legacy v0 type system is
still wired through the tree and alive in the consumer, and the data model
carries one game's field names.

**The v2 goal** is the apex of what v1 set out to be: an SDK a stranger would
pick for a new game tool. Concretely:

- A host that already has a window, an ImGui context and a Vulkan device can
  use Onyx's panels, viewers and renderer inside its own frame.
- A host that has none of that gets a complete shell in a dozen lines.
- A consumer that wants only the format layer links only the format layer,
  and its configure fetches nothing else.
- A second, unrelated module ships in the repo and proves the surface is
  sufficient, not merely reachable.
- One architecture. No legacy column, no compatibility shims, no singletons.

## 2. Goals, non-goals, anti-goals

**Goals**

1. Library over framework: every layer is a component the host composes; the
   ready-made shell is the top layer and optional.
2. Strict layering enforced by the build: a layer's headers live in its own
   include directory, so an illegal include does not resolve.
3. Game-agnostic data model: no field, enum value, comment or label names a
   game.
4. Correct GPU ownership: RAII handles, deferred destruction, no synchronous
   submit per draw or per upload, one orchestration for viewport and headless.
5. Embeddable renderer: adopts an external device, never touches global state.
6. Testable by construction: every layer has its own test target; the SDK
   tests against two synthetic modules; a consumer gets the same rig.
7. Repository that reads like the author's current standard (README, CLAUDE.md,
   check script, lanes, no history in comments).

**Non-goals (v2.0)**

- Runtime plugin loading (DLL modules). Static linking stays.
- A render thread. Submission stays on the main thread.
- Write-back/repack of archives.
- macOS. The platform files are kept compiling where they already are, nothing
  new is written for it.
- Porting GoWToolkit. That is the next project, with this SDK as its input.

**Anti-goals (what would make v2 a failure even if it builds)**

- A `compat` target or any header that exists to keep v1 code compiling.
- A singleton behind any `Get()`/`Instance()`.
- A public header that includes `imgui.h`, `volk.h`, `vk_mem_alloc.h`,
  `GLFW/`, `windows.h` or a libav header outside the layer that owns that
  dependency.
- A std::function hook on the shell that exists to let one host remove one
  menu item.
- A comment that narrates a task, milestone, fix round or review.

## 3. Constraints

- C++20, MSVC 2022 on Windows (primary), Clang/GCC on Linux (CI with
  lavapipe). CMake 3.28+, Ninja. CMake 4 must configure cleanly (v1 pins
  break under it).
- Vulkan 1.3: dynamic rendering, synchronization2, timeline semaphores.
  volk + VMA. Shaders compiled at build time (glslang) into SPIR-V headers.
- Dear ImGui docking branch at the v1 pin; GLFW 3.4; glm; toml++; cgltf;
  doctest; FFmpeg (optional); miniaudio (optional); lz4; stb.
- The v1.x goldens in `Tests/Golden/corpus` are the render oracle. What
  binds is the **PNG**, compared within the tolerances `VkOracleParity`
  uses today (copied into `Tests/Golden/tolerances.toml` in W0). The JSON
  batch reports and `pixelHash` beside the PNGs describe v1 batching and are
  re-blessed in W2 once the PNG gate passes; they are not an oracle. The
  corpus scenes are re-expressed as `Domain::Scene` by a TestKit fixture
  builder, and the PNG gate proves the re-expression. On Windows hardware
  the v1 tolerances apply unchanged; on lavapipe a separate tolerance set is
  recorded in the same file only after W2 has investigated why the v1
  parity run fails there today. A difference with no known cause is a
  failure, not a tolerance.
- GoWToolkit is not touched by this project. The design must still leave a
  home for everything it uses from the legacy column: ISO mounts, lz4,
  declarative schemas, ADPCM audio, video playback, a hex view.
- Commits: Conventional Commits, imperative Sentence-case subject ≤ 72 chars,
  no attribution to AI anywhere. Commit and push only when Jean asks.
- English only in code, comments and docs. PascalCase files and types.

## 4. Layers and targets

Nine static library targets. Each has its own `Source/<Layer>/` directory,
its own `include/Onyx/<Layer>/` public directory, its own `CMakeLists.txt`,
its own test executable, and links only the layers below it. No aggregate
`Onyx::Onyx` target exists; a consumer names the layers it uses.

```
Onyx::Core      Vfs, type identity, domain data, module contract, Workspace,
                jobs, events, diagnostics, settings, logging, probe, routing.
                Deps: glm, toml++, lz4. Nothing graphical, nothing OS-windowing.

Onyx::Gpu       Vulkan ownership: Device (own or adopted), handles, retire
                queue, frame ring, upload context, pipeline builder + cache,
                render target, swapchain. Deps: Core, volk, Vulkan-Headers, VMA.

Onyx::Render    Scene renderer over Gpu: GpuScene, materials, skinning,
                animation player, camera, overlays (grid, skeleton, gizmo),
                headless render. Deps: Core, Gpu, SPIR-V headers.

Onyx::Ui        ImGui components the host draws inside its own frame: theme,
                widgets, icons, texture pool, stock panels, viewers, viewer
                registry, command registry. Deps: Core, Gpu, Render, imgui.

Onyx::Shell     The ready-made application: native window, swapchain present,
                ImGui backend, dock, menu bar from commands, config, recents,
                lifecycle. Deps: Ui, glfw.

Onyx::Media     Video and audio playback: MediaSource (FFmpeg, no UI),
                AudioOutput (miniaudio), and a MediaView panel.
                Deps: Ui, ffmpeg, miniaudio. Optional: ONYX_WITH_MEDIA.
                FFmpeg is private: no public header names a libav type.

Onyx::Cli       The stock verbs (probe / list / extract / decode) over the
                VerbTable that Core defines. Deps: Core.

Onyx::Exchange  glTF 2.0 export of Domain::Scene; PNG and WAV writers,
                registered as `decode --to` exporters; it ships no verb.
                Deps: Core; cgltf and stb are private.

Onyx::TestKit   Tree goldens, decode smoke, PNG image compare (stb, no GPU),
                synthetic fixture builders (ISO, scenes, Prism/OnyxBox files).
                Deps: Core, stb.
```

Edges only point downward in this list. The CLI verb contract
(`Core::Cli::Verb`, `VerbTable`, `Run`) lives in **Core**, so `Render` and
`Exchange` contribute verbs without depending on `Onyx::Cli`; the host (or
`Shell::Run`) assembles the table: `cli.Add(Cli::StockVerbs())`,
`cli.Add(Render::CliVerbs())`, `Exchange::AddExporters(cli)`.
`ONYX_WITH_UI` implies `ONYX_WITH_GPU`; a UI without a GPU layer is not a v2
target. `UiContext::gpu` is a pointer: null in headless tests and
before a host created its device, and `textures` is null whenever `gpu` is.

**Dependency fetch is per component.** `FetchContent` for volk, Vulkan
Headers, VMA and glslang runs only when `ONYX_WITH_GPU` is on; imgui only with
`ONYX_WITH_UI`; glfw only with `ONYX_WITH_SHELL`; cgltf with `Onyx::Exchange`, which is
always built; FFmpeg only
with `ONYX_WITH_MEDIA` (miniaudio is vendored in `third_party/` and built with Media). An example may fetch what it alone needs:
`Examples/Embedded` fetches glfw for its own window under `ONYX_WITH_UI` and
`ONYX_BUILD_EXAMPLES`, even with the Shell off; no Onyx target links it. Defaults: the component
options are on whether Onyx is top-level or nested, so a nested consumer turns off what it does not
link. `ONYX_BUILD_TESTS` and `ONYX_BUILD_EXAMPLES` default ON only when top-level.

**Layer enforcement.** Each target's `target_include_directories` exposes only
its own `include/`. A header from another layer resolves only through that
layer's link, so `Core` physically cannot include `Ui`. A compile test per
layer (`Tests/<Layer>/Standalone.cpp`, one TU per public header, with only
that layer's includes) replaces the grep-based `LayerGuard`.

**Precompiled headers** per target (std + the layer's own third-party set).
No unity builds.

## 5. Core

### 5.1 Vfs — stateless files

```cpp
namespace Onyx::Vfs {
class IFile {
public:
    virtual ~IFile() = default;
    virtual uint64_t Size() const = 0;
    virtual size_t   ReadAt(uint64_t offset, std::span<std::byte> dest) const = 0;
};
class Reader;        // value type: shared_ptr<IFile> + cursor + endian helpers.
                     // Every parser reads through a Reader it owns. Two Readers
                     // over one IFile never interfere.
class SliceFile;     // IFile view [base, base+size) over a parent IFile.
class OsFile;        // ReadAt via pread/ReadFile(OVERLAPPED); opened with the
                     // native wide path on Windows.
class MemoryFile;    // owns a byte vector.
class IVirtualFileSystem {
public:
    virtual ~IVirtualFileSystem() = default;
    virtual std::vector<DirEntry> List(std::string_view dir) const = 0;
    virtual std::shared_ptr<IFile> Open(std::string_view path) const = 0;
    virtual bool Exists(std::string_view path) const = 0;
};
}
```

`IsoFileSystem` is kept (GoW2 mounts ISO), rewritten against `Reader`, with
bounds checks, a cycle guard on directory extents, a working `List`, and tests
over a synthetic ISO built by TestKit. `lz4` stays in Core as
`Vfs::Lz4Decompress(span, span)` with a test; it is not linked for nothing.

### 5.2 Type identity

```cpp
struct TypeKey  { std::string value; };    // "obx.image" — persisted identity
struct TypeId   { uint32_t value = 0; };   // process-lifetime handle
struct TypeSpec { std::string key, label; MediaKind media; IconId icon; Color color; };

class TypeCatalog {                        // owned by Workspace; no Get()
public:
    TypeId Register(std::string_view moduleId, const TypeSpec&);   // "<module>.<key>"
    TypeId Find(std::string_view key) const;
    const TypeSpec& Spec(TypeId) const;
    MediaKind Media(TypeId) const;
    size_t Count() const;
};
class TypeRegistrar {                      // handed to IGameModule::RegisterTypes
public:
    TypeId Add(const TypeSpec&);           // pre-bound to the module's namespace
};
```

`IconId` is a small integer naming a glyph in the SDK's icon set
(`Core/include/Onyx/Core/Icons.h`, ~60 named entries). The glyph table itself
(SF Symbols codepoints) lives in `Ui` as a generated source, never in a public
header. A module names an icon; `Ui` resolves it.

### 5.3 MediaKind

Closed enum of what the SDK can route: `Unknown, Image, Mesh, Material,
Skeleton, Animation, Audio, Video, Text, Table, Script, Map, Shader,
Container, Raw`. `Text` and `Table` are added; the PS2 format names in the
comments are gone. Openness comes from the decoder registry's `Any` slot and
the viewer registry's type-keyed entries (§7.4), not from the enum.

### 5.4 Domain data model

Plain value types in `Onyx::Domain`. Nothing here names a game, a file
offset, a bit-stream layout or a GPU layout.

```cpp
struct MeshPart {
    std::string name;
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;          // may be empty
    std::vector<glm::vec4> tangents;         // may be empty
    std::vector<glm::vec2> uv0, uv1;         // may be empty
    std::vector<glm::vec4> colors;           // may be empty
    std::vector<glm::u16vec4> joints;        // may be empty (unskinned)
    std::vector<glm::vec4>    weights;       // same length as joints
    std::vector<uint32_t> indices;           // triangle list
    std::vector<uint16_t> jointMap;          // local joint → skeleton joint
    uint32_t materialIndex = 0;
    PartFlags flags = PartFlags::None;       // Sky (no depth write), Hidden
    uint64_t userTag = 0;                    // opaque to the SDK; module-defined
    BoundingBox bounds;
};
struct Joint    { std::string name; int32_t parent = -1; glm::mat4 localBind{1}; glm::mat4 inverseBind{1}; };
struct Skeleton { std::vector<Joint> joints; };

struct AnimChannel { uint32_t joint; std::vector<Key<glm::quat>> rotation;
                     std::vector<Key<glm::vec3>> translation, scale; };
struct AnimClip    { std::string name; float duration; std::vector<AnimChannel> channels; bool additive = false; };
struct AnimationSet{ std::vector<AnimClip> clips; };

enum class TextureRole : uint8_t { Diffuse, Normal, Occlusion, Gloss, Height, Scatter, Detail, Emissive, EnvMap, Opacity };
enum class BlendMode   : uint8_t { Opaque, Alpha, Additive, Subtractive, EnvMap };
enum class NormalEncoding : uint8_t { Xyz, Xy, ReconstructZ };   // from v1.3's generic encoding

struct Material { std::string name; glm::vec4 baseColor{1}, tint{1}; BlendMode blend; glm::vec2 uvOffset{0};
                  float metallic = 0; NormalEncoding normalEncoding; std::map<TextureRole, uint32_t> textures; };
enum class PixelFormat : uint8_t { Rgba8, Bc1, Bc3, Bc4, Bc5, Bc7, RgbaF16 };   // block formats upload as-is
struct Texture  { std::string name; uint32_t width = 0, height = 0; PixelFormat format = PixelFormat::Rgba8;
                  std::vector<std::byte> pixels; bool IsValid() const; /* checks size == expected for format */ };
struct Text     { std::string text; std::string language; };   // language: "json", "lua", ""
struct Scene    { glm::mat4 transform{1}; std::shared_ptr<const Skeleton> skeleton;
                  std::shared_ptr<const AnimationSet> animations; std::vector<MeshPart> parts;
                  std::vector<Material> materials; std::vector<Texture> textures; BoundingBox bounds; };
struct Audio    { uint32_t sampleRate, channels; std::vector<float> samples; };   // interleaved PCM
struct Table    { std::vector<std::string> columns; std::vector<std::vector<std::string>> rows; };
```

Where a module's format has a rest pose as Euler/Q.14 fixed point, an
inverse-bind table indexed through a side array, or a sample bit-stream, the
**module** converts at decode time. The three v1 types that encoded this
(`ObjectData`, `AnimationData`, `GpuVertex`) and the per-part `isRigid`,
`meshHash`, `textureLayer` and the scene-level `flipZ` default are gone;
`flipZ` becomes the module setting `transform` to a Z-flip when its
coordinate system needs one.

### 5.5 Module contract

Kept from v1 with three changes: no `TypeCatalog&` leaking into the
constructor, an `Any` decoder slot, and `ParseContainer` reads through a
`Reader`.

```cpp
class IGameModule {
public:
    virtual ~IGameModule() = default;
    virtual ModuleInfo  Info() const = 0;
    virtual ProbeResult Probe(const ProbeInput&) const = 0;
    virtual void        RegisterTypes(TypeRegistrar&) = 0;
    virtual void        RegisterDecoders(DecoderRegistry&) = 0;
    virtual std::vector<MountSpec> Mounts() const { return {}; }
    virtual ParseResult ParseContainer(ContainerContext&) = 0;
    virtual void        RegisterSettings(SettingsSchema&) {}        // typed module settings, §5.8
};

class DecoderRegistry {
public:
    void Scene(TypeId, SceneDecoder);   // → unique_ptr<Domain::Scene>
    void Image(TypeId, ImageDecoder);   // → unique_ptr<Domain::Texture>
    void Text (TypeId, TextDecoder);    // → Domain::Text {text, language}
    void Audio(TypeId, AudioDecoder);   // → unique_ptr<Domain::Audio>
    void Table(TypeId, TableDecoder);   // → unique_ptr<Domain::Table>
    void Any  (TypeId, std::string_view capability, AnyDecoder);  // → shared_ptr<void> + capability key
    Capabilities Of(TypeId) const;      // bitset + capability keys; the ONE routing source
    // Decode* for each slot, as v1.
};
```

`Capabilities Of(TypeId)` is what the CLI's `decode`, the UI's viewer routing
and TestKit's smoke all consult. The v1 `RouteForType` and its hand copy in
`CmdDecode` are replaced by this single function.

### 5.6 Workspace and Document

As v1 (`Workspace` owns modules, catalog, decoders, settings, events, jobs,
documents; `Document` carries tree, file table, diags, module state), with:

- `Workspace` owns its `TypeCatalog`; the constructor takes a
  `WorkspaceOptions{ workerThreads, settingsDir }`.
- `AssetEntry` loses `wadName`, `hash`, `assetNode`; gains
  `std::string subtitle` (what `wadName` was used for in the UI) and
  `std::vector<Diag>` is attached by reference to failed nodes.
- `Open` always probes. With one module registered, a probe below the floor
  still refuses the file; the single-module shortcut that let any file open
  is gone.
- `RemoveModule(id)` closes its documents and unregisters its types' decoders.
- Workspace settings are a member swapped only when no job is running; the
  comment-guarded race is replaced by a lock plus a documented precondition
  that `SetWorkspaceSettingsPath` is called before `Open`.

### 5.7 Jobs, events, diagnostics, progress, threading

Moved verbatim from v1: `JobQueue`, `JobHandle`, `Progress`, `EventBus`,
`Subscription`, `DiagSink`, `Diag`, `Severity`, `Threading::MarkMainThread`.
The v1 `jobs_test` failure on Linux (`weak.expired()` at the handle/queue
teardown race) is fixed in the move. `TaskManager`, `EventManager`,
`FrameScheduler` (moves to Ui as `FrameClock`), `Metrics`, `SessionLog`
(folded into Logger's file sink) are deleted.

### 5.8 Settings, config, logging

- `Settings` (TOML, toml++) gains atomic save (temp file + rename), a
  `Load` that on parse error keeps the file untouched, copies it to
  `<name>.broken.toml` and starts from defaults, and `Set` marks dirty so a
  periodic `SaveIfDirty()` from the host loop persists without waiting for a
  destructor.
- `SettingsSchema` lets a module or the shell declare typed keys with
  defaults (`Bool("gowr.useExternalIndex", false)`), so the UI can render a
  settings page for a module it knows nothing about.
- `AppConfig` is deleted. The shell's own typed settings are an ordinary
  `SettingsSchema` owned by `Shell`.
- `Logger` is an object (`Core::Log`) with sinks; a process-wide default
  instance exists for convenience (`Log::Default()`) because logging is
  legitimately process-wide — this is the one global in the SDK, and it holds
  no resources but a mutex. Ring buffer is a deque with O(1) eviction; the
  file sink keeps its handle open; level filtering happens before formatting.
- `PathUtils` becomes `Core::Paths` using `std::filesystem::path` only; the
  Windows include moves to the `.cpp`.

### 5.9 Probe and routing

`RankProbes` as v1. `Capabilities` (§5.5) is the routing source.

### 5.10 Declarative schema

One DSL, `Core::Schema`, replacing v1's two (`Schema/StructDef`+`NodeInstance`
and `Schema/AssetFormat`). A schema is data: a struct of typed fields,
counted arrays and nested structs with offsets expressed as constants or as
references to earlier fields. The engine reads a schema over a `Vfs::Reader`
into a `SchemaNode` tree (field name, type, value, byte range) that the
Inspector renders and the CLI prints as JSON. Integers keep their width
(`int64`/`uint64` are not stored as `double`). A module registers a schema for
a type through `DecoderRegistry::Schema(TypeId, SchemaRef)`, and `Capabilities`
reports it. GoW2's MDL is the known consumer; the SDK tests it over a
synthetic layout in Prism.

The base is v1's `Schema/StructDef` + `NodeInstance` (the pair the consumer
uses); `AssetFormat` is dropped. The 2.0 feature set is fixed-width integers
(8 to 64 bits, signed and unsigned), float32/64, fixed-length strings, byte
arrays, nested structs, counted arrays whose count is a constant or an
earlier integer field, offsets that are absolute, relative to the struct
start or read from an earlier field, per-schema endianness, and explicit
padding. Bitfields, conditionals and variants are out of 2.0.

## 6. Gpu

Everything Vulkan the SDK owns, embeddable by construction.

```cpp
namespace Onyx::Gpu {
struct ExternalDevice { VkInstance; VkPhysicalDevice; VkDevice; VkQueue; uint32_t queueFamily;
                        PFN_vkGetInstanceProcAddr; PFN_vkGetDeviceProcAddr; };
class Device {
public:
    static Result<std::unique_ptr<Device>> Create(const DeviceOptions&);   // own instance/device
    static Result<std::unique_ptr<Device>> Adopt(const ExternalDevice&);  // host's device
    const DeviceTable& Fn() const;     // per-device dispatch (volkLoadDeviceTable); never the global table
    VmaAllocator Allocator() const;
    Queue& GraphicsQueue();            // one queue, internally mutexed submit
    RetireQueue& Retire();
    ~Device();                         // waits idle, drains retire, destroys in order
};
template<class T> class Unique;        // RAII handle; destructor enqueues on Retire with the current fence
class RetireQueue  { void Enqueue(Deleter, FenceValue); void Collect(FenceValue completed); };
class FrameRing    { Frame& Begin(); void End(Frame&); /* N frames; per-frame pool, fence, transient ring */ };
class UploadContext{ Buffer Upload(span); Image Upload(const Domain::Texture&); FenceValue Flush(); };
class PipelineBuilder { /* fluent; VkPipelineCache loaded from and saved to a cache file */ };
class RenderTarget { /* MSAA color + depth + resolve; Resize() retires the old images */ };
class Swapchain    { /* present path from v1 Window.cpp, with every VkResult handled */ };
}
```

**Adopted devices.** `Adopt` validates Vulkan 1.3 with `dynamicRendering`,
`synchronization2` and `timelineSemaphore` enabled and refuses with a
`Result` error otherwise; there is no fallback path. Onyx records into its
own command buffers from its `FrameRing` and submits them itself.
`FrameRing::End` and `UploadContext::Flush` return a timeline value on Onyx's
own timeline semaphore (`Device::GraphicsQueue().Timeline()`); the host waits
on that value (as a submit wait or a fence-equivalent) before it samples an
Onyx `RenderTarget` in its own pass. In the other direction, before Onyx
re-renders a target the host is still reading, the host signals its own
timeline semaphore and the next frame waits on it through `Frame::AddWait`. The queue the host passes
in `ExternalDevice` is used only under the queue mutex: through
`Device::GraphicsQueue().Submit()` and `Present()`, and by code that holds
`Queue::Lock()`. The one such caller is Onyx::Ui's `ImGuiVulkanBackend`:
imgui's Vulkan backend submits font texture uploads to the queue and waits
for it idle by itself, so `Record` and `RebuildFonts` hold `Queue::Lock()`
around the imgui calls. The host either hands Onyx a dedicated queue of
the same family (preferred, and what `Examples/Embedded` does) or routes its
own submissions through that same `Submit` (or holds `Queue::Lock()`). This precondition is stated in
the header and observable through `Device::SubmitCount()`, which counts the
submits that went through `Queue::Submit`; a host's direct submits cannot be
detected.

Rules enforced by this layer:

- No `vkQueueWaitIdle`/`vkDeviceWaitIdle` outside `Device::~Device` (owned
  devices only; an adopted device waits on its own timeline), `Swapchain::Recreate`
  and `Swapchain::~Swapchain`, which use `Queue::WaitIdle` under the queue mutex.
  The exception is imgui's own texture upload inside `ImGuiVulkanBackend`
  (Ui), which calls `vkQueueWaitIdle` while the backend holds `Queue::Lock()`.
- No synchronous submit. `UploadContext::Flush` submits once and returns a
  timeline value; callers that need the data wait on the value, and in
  practice the frame ring already orders it.
- All mapped per-frame data (UBOs, palettes, overlay vertices) lives in the
  frame's transient ring; nothing is single-buffered.
- `robustBufferAccess` is enabled on devices Onyx creates; on adopted devices
  the renderer validates joint indices on upload instead.
- Every `VkResult` is checked; failure returns `Result<T>` with the result
  code, never a bool with a logged string.

## 7. Render

```cpp
namespace Onyx::Render {
class SceneRenderer {
public:
    explicit SceneRenderer(Gpu::Device&, const RendererOptions&);     // creates pipelines once, from cache
    GpuScene Load(const Domain::Scene&, Gpu::UploadContext&);          // one Flush; materials deduped by hash
    void Draw(Gpu::Frame&, Gpu::RenderTarget&, const GpuScene&, const View&, const Overlays&);
};
struct View     { glm::mat4 view; Projection proj; ShadingMode mode; glm::vec4 clear; std::optional<Gradient> background; };
struct Overlays { bool grid; bool skeleton; glm::vec4 gridColor, boneColor; std::optional<Gizmo> gizmo; };
class AnimationPlayer { void Set(const AnimationSet&, uint32_t clip); bool Advance(float dt); std::span<const glm::mat4> Palette() const; };
Result<Image> RenderToImage(Gpu::Device&, const Domain::Scene&, const View&, const Overlays&, Extent);
}
```

- **One orchestration.** The interactive viewport and `RenderToImage` both
  call `SceneRenderer::Draw`. Headless output can draw grid and skeleton.
- **One projection convention.** `Projection::Perspective(fov, aspect, near,
  far)` bakes the Vulkan clip-space flip; nothing asserts on the sign of
  `proj[1][1]`.
- **Camera** keeps v1's `Camera` with a single `FocusOn(bounds)`; the CLI and
  viewport call the same one.
- **Materials** are deduplicated; descriptor sets are per material, not per
  part. Vertex layout is chosen here (interleaved, 48 bytes without skinning,
  72 with) from the Domain streams.
- **Skinning** validates `jointMap` and joint indices on `Load` and reports a
  diag instead of truncating.
- **Animation** samples `AnimClip` keyframes; clip baking is a per-clip lazy
  bake with an LRU of eight.
- The four v1 pipeline creators collapse into `PipelineBuilder` calls.
- `RenderContext`/`AddPass` is deleted. The raw floor is `Onyx::Gpu`, which
  is public.

## 8. Ui

ImGui components drawn by the host inside its own frame.

### 8.1 Contracts

```cpp
namespace Onyx::Ui {
struct UiContext {              // passed to every Draw; built by the host or the Shell once per frame
    Modules::Workspace& workspace;
    Gpu::Device*        gpu;       // null when the host has no GPU layer
    Gpu::Frame*         frame;     // the host's current frame; the Viewport records into it
    TexturePool*        textures;  // null when gpu is null
    Theme&              theme;
    CommandRegistry&    commands;
    ViewerRegistry&     viewers;
    PanelHost&          panels;
    FrameClock&         clock;
    float               dt;
    IAudioOutput*       audio = nullptr;
    const FontSet*      fonts = nullptr;   // the host's atlas fonts; null with ImGui's default font
};
class IPanel {
public:
    virtual ~IPanel() = default;
    virtual std::string_view Id() const = 0;      // "onyx.browser"
    virtual std::string_view Title() const = 0;
    virtual void Draw(UiContext&) = 0;
    bool visible = true;
};
class IDocumentView : public IPanel {           // a viewer tab bound to one entry
public:
    virtual Modules::DocumentId Document() const = 0;
    virtual Modules::NodePath   Node() const = 0;
};
}
```

`fonts` points at the `FontSet` the host built (`BuildFonts` or
`BuildThemeFonts`; the Shell and `Examples/Embedded` set it from their
atlas). The text and hex viewers push `fonts->mono` when it is set.

### 8.2 Viewer registry

```cpp
struct ViewerSpec {
    std::string id;                                      // "onyx.image"
    std::variant<MediaKind, TypeKey> target;             // what it opens
    int priority = 0;                                    // higher wins on ties
    std::function<std::unique_ptr<IDocumentView>(OpenRequest&)> create;
};
class ViewerRegistry {
public:
    void Register(ViewerSpec);
    void Unregister(std::string_view id);
    const ViewerSpec* Route(const TypeCatalog&, const DecoderRegistry&, TypeId) const;
};
```

Stock viewers register themselves by `MediaKind` (Image, Text, Mesh/Scene,
Audio, Table, Raw → hex). A module ships a viewer for its own `TypeKey`
through an extension the host adds (`shell.AddExtension(std::make_unique<
MyModuleUi>())`, an `IShellExtension` that registers viewers, or the same
registration done by an embedding host on its `ViewerRegistry`), so `Core` stays UI-free and a module author writes UI only
when they want to. The `OpenRequest` carries the decoded payload (any slot of
§5.5) already decoded on the job lane.

### 8.3 Stock panels

`DocumentBrowser` (clipped, no per-frame strings), `Inspector` (schema-driven
over `TypeSpec` + capabilities), `Problems` (drains DiagSink), `Log` (clipped
over the ring), `Settings` (renders every `SettingsSchema`), `Viewport`
(camera + draw only), `Transport` and `Timeline` (animation controls, separate
panels), `Image`, `Text`, `Hex`, `Audio`, `Table`, `Gallery` (not registered
by default; a host opts in).

### 8.4 Commands

```cpp
struct Command { std::string id; std::string label; std::string menuPath;   // "File/Open…"
                 std::optional<Shortcut> shortcut; std::function<bool(UiContext&)> enabled;
                 std::function<void(UiContext&)> run; };
class CommandRegistry {
    void Add(Command); void Remove(std::string_view id);
    Result<void> Override(std::string_view id, Command);   // ui.command.unknown
    Result<void> Run(std::string_view id, UiContext&);      // ui.command.unknown
    const std::vector<Command>& All() const; const Command* Find(std::string_view id) const;
    void PollShortcuts(UiContext&);
};
```

The shell's menu bar is rendered from this registry. A host removes or
replaces a command by id. Shortcuts are bound. There are no `SetXHandler`
hooks and no `ONYX_HAS_*` macros.

### 8.5 Theme, widgets, icons, textures

- One writer: `Theme` resolves tokens and applies them to `ImGuiStyle`.
  Transitions are inside `Theme::Update(dt)`. Settings read and write
  `Theme`; nothing writes `style.Colors` directly. Colour overrides persist
  by token name, not `ImGuiCol` integer.
- `Widgets` is the set the stock panels themselves use; a widget is added
  when a panel needs it twice.
- Icons: SF Symbols TTF is an asset copied beside the executable by a CMake
  helper the consumer calls (`onyx_install_assets(target)`); the codepoint
  table is a generated `.cpp`.
- `TexturePool` (one, in Ui) hands ImGui texture ids and retires through
  `Gpu::RetireQueue`. Binding an image to an `ImTextureID` goes through an
  `ITextureBinder`; the stock implementation calls the ImGui Vulkan backend
  (`ImGui_ImplVulkan_AddTexture`), and a host with its own backend supplies
  its own binder.
- **ImGui ownership.** `Onyx::Ui` draws into whatever `ImGuiContext` is
  current; it never creates one (the Shell does). `cmake/Deps/imgui.cmake`
  uses an existing `imgui::imgui` target when the host defines one before
  adding Onyx, and fetches the pinned docking commit only when none exists,
  so an embedding host never links two copies.
- `FrameClock` (from v1 `FrameScheduler`) provides `RequestRedraw` for
  hosts that render on demand.

## 9. Shell

```cpp
int main(int argc, char** argv) {
    Onyx::Shell::Shell shell({ .appName = "MinimalViewer" });       // configDir defaults to Paths::UserConfig(appName)
    shell.Workspace().AddModule(std::make_unique<OnyxBox::Module>());
    shell.AddExtension(std::make_unique<MyExtension>());            // optional
    shell.Commands().Remove("onyx.file.exportAll");                 // optional
    return shell.Run(argc, argv);
}
```

`Run` routes the command line through `RouteArgs`: no argument opens the
window; exactly one argument that is not a verb name and names an existing
regular file opens the window and calls `Open` on that file (double-clicking
an associated file); any other command line runs the CLI.

`Shell` composes `NativeWindow` (GLFW + decoration), `Gpu::Device`,
`Gpu::Swapchain`, `ImGuiBackend`, `Dock`, `MenuBar` (from commands),
`Recents`, `Settings` with periodic save, and a `LifecycleDelegate`
(`WillClose() → bool`, `FilesDropped(paths)`). Close runs the delegate, waits
the device idle, and returns from `Run`; nothing calls `exit`. The v1
`Window.cpp` splits into these parts; no part exceeds ~300 lines.

`Shell::Step()` runs one iteration of the loop (events, UI, present) and
returns `Result<bool>`; `Run` loops over it, and tests and hosts that own the
loop call it directly. `ShellOptions::hiddenWindow` creates the window hidden,
so window tests run on machines that can create a window but not show one.
Dialogs run between frames and `SetDialogs` injects a replacement.

Platform dialogs (`OpenFile`, `OpenFolder`, `SaveFile`) are a Shell service
with Windows and Linux (zenity/xdg portal) backends.

## 10. Media

Video is an **entry** of `MediaKind::Video` inside a module's container, never
a loose file: the viewer request carries the entry's `file` and `range`, and
Media reads that byte range through the `Vfs::IFile`, never through a path.

`MediaSource` owns the FFmpeg contexts and exactly one decode thread; the UI
thread never touches an `AVCodecContext` and only queues commands (`Open`,
`Play`, `Pause`, `Stop`, `Seek`) and reads snapshots (`State`, `Position`,
`Info`, `TakeFrame`). Packets and frames are RAII, and no public header
includes a libav or miniaudio header (an opaque `std::unique_ptr<Impl>`), so a
consumer of `Onyx::Media` compiles without FFmpeg's headers on its include
path. The decode thread keeps a mailbox of the newest decoded frame plus at
most four ahead of the clock, and blocks there, so a paused source costs
nothing. The clock is the audio clock (frames the playback device consumed)
when the clip has audio, and a steady clock otherwise. While the audio starves
the thread reads ahead, holding compressed packets (at most 256 packets and
32 MiB, never more decoded frames) to reach audio stored behind the video;
when the audio ends first, or stays starved for 250 ms with nothing more to
read, the steady clock takes over from the audio position and the video plays
out. Errors are `media.open`, `media.stream`, `media.seek` and `media.audio`.

`AudioOutput` implements `Ui::IAudioOutput` over miniaudio (`Play`, `Stop`,
`Pause`, `Resume`, `Seek`, `Paused`, `Playing`, `Position`). `MediaView` is an
`IDocumentView` with id `onyx.media.<doc>.<name>` that pulls frames into the
`TexturePool`; `RegisterMediaViewer(ViewerRegistry&)` registers `onyx.media` for
`MediaKind::Video`. A host wires Media with
`shell.SetAudioOutput(std::make_unique<Media::AudioOutput>())` and
`Media::RegisterMediaViewer(shell.Viewers())`. The v1 `AdpcmDecoder` (Sony
VAG) is deleted: a module decodes its own audio encodings into
`Domain::Audio`, and the stock `Audio` viewer plays PCM. Nothing in Media
knows a console's codec.

## 11. Cli

`Core::Cli::Run(Workspace&, const VerbTable&, argc, argv, out, err)`. The
`Verb` contract and `VerbTable` live in Core (§4); `Onyx::Cli` provides the
stock verbs, `Render` provides `render`, and `Exchange` provides the `--to
gltf|png|wav` exporters that `decode` consults through a `Core::Exporters`
registry; the v1 `std::function` parameters are gone.

Entry resolution is part of Core's Cli contract:
`Cli::ResolveEntry(const Modules::Document&, std::string_view target, bool
first, std::ostream& err)` in `Onyx/Core/Cli/Resolve.h`, with
`Modules::FindAllByName`. An entry is addressed by `@<i>/<j>/...` (a
`NodePath`) or by name. A name shared by several entries fails with
`cli.ambiguous`, which lists their `@paths` and exits `kUsage`, unless `--first`
selects the earliest in depth-first order. `extract` takes no entry names and
reports failures in its exit code. The usage string uses `argv[0]`.

`Render::CliVerbs()` provides `render <file> <entry|@path> --out <png> [--width
N] [--height N] [--clip N --time S] [--grid] [--first] [--game <hint>]`. It
writes through the table's `png` exporter; a table without one fails with
`render.noPng` and exits `kUsage`.

## 12. Exchange

Exchange ships no verb. Its entry point is `Exchange::AddExporters(Cli::VerbTable&)`,
which registers `gltf` (Mesh), `png` (Image) and `wav` (Audio) as `decode --to`
exporters. `WriteGltf(Scene, path, GltfOptions)` reads `Domain::Scene` streams
directly and writes one `.glb` or a `.gltf` with a sibling `.bin`, including
skin and one animation per non-additive `AnimClip`; `WritePng(Texture, path)`
takes Rgba8; `WriteWav(Audio, path)` writes 16-bit PCM. Errors are
`exchange.gltf.empty`, `exchange.gltf.write`, `exchange.png.format`,
`exchange.png.write` and `exchange.wav.write`. No public header includes cgltf
or stb.

## 13. TestKit and the two synthetic modules

TestKit keeps goldens, decode smoke and image compare, and adds fixture
builders for a synthetic ISO and a synthetic animated scene (today only in the v1
tree, `legacy/Tools/OnyxOracle`).

Two modules live in `Examples/` and both are used by the SDK's own tests:

- **OnyxBox** (`obx`), ported: container, pak mount, image, text, mesh. Its
  type keys carry the module id (`obx.*`).
- **Prism** (`prm`), new: a palette-centric container (`PRM1`) with six
  section kinds: palette (`prm.palette`, a `Table` that also serves the
  `prism.palette` `Any` capability), indexed image, text manifest, audio clip,
  a `Table` of entries and a stats record read through the declarative schema.
  Its type keys carry the module id (`prm.*`). Its settings are
  `prism.transparentIndexZero` (Bool) and `prism.gain` (Double, 0 to 4). It
  ships a `PrismUi` extension (`Prism::Ui::Extension`) with a palette viewer
  routed by that capability. Prism is the generality
  proof from the v1 design (§13 there) that was cut; the exam rule binds: a
  gap Prism hits is an SDK task, never a workaround in Prism.

## 14. Examples

- `MinimalViewer`: the Shell with OnyxBox, Prism, `Prism::Ui::Extension` and,
  under `ONYX_WITH_MEDIA`, the Media audio output and viewer, in about thirty
  lines.
- `Embedded`: a host with its own GLFW window, ImGui context and Vulkan
  device that adopts them into Onyx and draws `DocumentBrowser` + `Viewport`
  inside its own frame. This is the Icon-Composer / SF-Symbols scenario.
- `OnyxCli`: `Cli::Run` over a table holding `Cli::StockVerbs()`,
  `Render::CliVerbs()` and `Exchange::AddExporters`, with both modules.
- `CoreOnly`: a consumer linking `Onyx::Core` only, built by the consumer
  test from outside the tree; its configure must fetch no GPU/UI dependency.

## 15. Repository

```
CMakeLists.txt              options, components, FetchContent per component
CMakePresets.json           windows-msvc, linux-clang; Debug/Release
cmake/                      Shaders.cmake, Assets.cmake, Warnings.cmake, Deps/*.cmake (one per dependency)
Source/<Layer>/             CMakeLists.txt, include/Onyx/<Layer>/, *.cpp, Shaders/ (Render)
Examples/{MinimalViewer,Embedded,OnyxCli,OnyxBox,Prism}
Tests/<Layer>/              one executable per layer + Standalone.cpp compile test
Tests/Golden/               frozen corpus
Tests/Consumer/             external consumption gate (Core-only and full)
Tests/Oracle                the OnyxOracle tool (Tools/ and tools/ cannot coexist on a case-insensitive filesystem)
Tools/ColdStart
tools/check.py              configure + build + ctest + lint; verdict is the exit code
docs/                       architecture.md, consuming.md, modules.md, design/, superpowers/
third_party/                fonts, stb, miniaudio (vendored as today)
README.md  CLAUDE.md  CHANGELOG.md  LICENSE
```

- `README.md` follows the AquaKit pattern: centred header (logo 256, h1,
  bold tagline, badges for platform / C++20 / CMake / Vulkan), then prose
  sections — what it is, how it is built (the layers in one paragraph and one
  hand-written SVG), status, build and run, consuming (FetchContent + the
  component options), a "Where to look" table, and the "Made by JeanxPereira"
  footer. No feature matrix.
- `CLAUDE.md` is short: what Onyx is, the principle (layers compose; the
  module writes format knowledge; nothing names a game), numbered rules,
  tools (`check.py`, presets), and a "Where to look" table. No state.
- `tools/check.py` is the single verdict: configure the preset, build,
  ctest, run the lint (header hygiene: forbidden includes per layer, no
  `Get()` singletons, no game names in `Source/`), exit non-zero on any
  failure.
- CI runs `check.py` on Windows and Linux (lavapipe), with build caching.
- `Version.h` is generated into the build directory, not written into the
  source tree.
- No comment in the tree references a task, milestone, fix round, review,
  or a previous version of the code.

## 16. Migration plan

Break-and-port in waves on branch `v2`, branched from `origin/main` at
`v1.3.3` (`f2609ae`), which carries the generic normal encoding this design
keeps and the macOS fixes. `feat/host-ui-hooks` is not merged; the
uncommitted `Version.h` line-ending change is discarded; this document and
the survey are the first commit on `v2`. Each wave ends with the tree
building, every ported test green, and the replaced v1 code deleted. The v1
tree stays available as `v1.3.3` for reference; nothing in `v2` includes it.

| Wave | Content | Gate |
|---|---|---|
| **W0 Skeleton** | New repo layout, component options, per-layer targets with empty sources, PCH, presets, `check.py`, CI, README/CLAUDE.md drafts. Old tree moved to `legacy/` (untracked by targets) for reference during the port, deleted in W5. | `check.py` green on both OS with empty layers |
| **W1 Core** | Vfs stateless + ISO + lz4; TypeCatalog object; Domain model; module contract with `Any` and `Capabilities`; Workspace; jobs/events/diags moved; Settings atomic + schema; Log; OnyxBox ported; Core tests ported and extended (ISO, Slice, lz4, Capabilities, Settings corruption). | Core tests green; `Tests/Consumer/CoreOnly` configures with no GPU/UI fetch; Standalone compile test |
| **W2 Gpu + Render** | Device own/adopt, handles, retire, frame ring, upload context, pipeline builder + cache, render target, swapchain; SceneRenderer one orchestration; AnimationPlayer over AnimClip; RenderToImage; OnyxOracle ported. | Frozen goldens pass within v1 tolerances; VkAnimation gate ports; a resize-under-validation test; a submit-count test (Load = 1, Draw = 0). Oracle in `Tests/Oracle`; lavapipe uses the `[software]` profile |
| **W3 Ui + Shell** | UiContext, IPanel, ViewerRegistry, CommandRegistry, Theme single writer, widgets, icons, TexturePool, stock panels and viewers, Viewport split; Shell composition; dialogs; MinimalViewer. | MinimalViewer opens OnyxBox files; theme and settings tests; `Embedded` example builds and draws (manual check on Windows, build-only on CI) |
| **W4 Media + Cli + Exchange + Prism** | MediaSource/AudioOutput/MediaView; Cli verb table with Render/Exchange registration; glTF animation, PNG, WAV; Prism module + PrismUi; TestKit fixture builders. | Prism opens, decodes, renders and shows its palette viewer with zero SDK edits logged during its build; CLI goldens; Linux CI green |
| **W5 Repo** | Delete `legacy/`; README final with SVG; CLAUDE.md; docs; CHANGELOG 2.0.0; tag. | `check.py` green on both OS; lint finds zero game names and zero forbidden includes |

W3 location notes: Ui sources are in `Source/Ui/` (headers in `Source/Ui/include/Onyx/Ui/`), Shell in `Source/Shell/`; `MinimalViewer` and `Embedded` are in `Examples/`. The suites are `Ui.Unit`, `Ui.Gpu`, `Shell.Unit`, `Shell.Window`, `Examples.Build` and `Examples.Embedded`. The Gallery panel moves to W4, where Prism's UI is its second consumer.

W4 location notes: Media sources are in `Source/Media/` (headers in `Source/Media/include/Onyx/Media/`), Exchange in `Source/Exchange/` and the `render` verb in `Source/Render/CliVerbs.cpp`; entry resolution is `Source/Core/Cli/Resolve.cpp`. Prism is `Examples/Prism/` with `PrismUi` in `Examples/Prism/Ui/`, and `OnyxCli` is `Examples/OnyxCli/`. The Gallery panel is `Source/Ui/Panels/Gallery.cpp` (id `onyx.gallery`, hidden by default, registered by the host). The suites added are `Exchange.Unit`, `Media.Unit`, `Media.Gpu`, `Prism.Unit` and `Examples.Cli`; the CLI goldens are in `Tests/Cli/Goldens`.

**Lanes.** Sonnet builders, one task each, each in its own worktree with
disjoint files; the lane that provides headers commits early and dependent
lanes branch from it; a Sonnet reviewer reads only the diff; merges are
`--no-ff` followed by `check.py`. Jean merges and pushes.

## 17. What is left to the builder

- Exact `Result<T>` type (expected-like; `std::expected` is C++23, so a
  small `Onyx::Core::Result` is written once).
- `DeviceTable` shape (volk's `VolkDeviceTable` or a hand-rolled subset).
- Transient ring sizes, frames-in-flight count (default 2), LRU sizes.
- Shortcut syntax for commands, dock default layout format.
- Exact Prism binary layout (synthetic; must exercise every decoder slot
  including `Any` and `Table`).
- Lint implementation language (Python, like `check.py`).
- Which v1 widgets survive into `Widgets` (rule in §8.5 decides).

## 18. Decisions recorded as the author's calls

Made during the survey conversation on 2026-10-30:

- Library over framework (inverts the v1 §1 "Framework" decision).
- Break-and-port to v2; no compat layer.
- VideoPlayer stays, as optional `Onyx::Media`, split into source and view.
- ISO, lz4 and declarative schemas stay in the SDK and gain tests; game
  encodings (VAG/ADPCM, Q.14, sample bit-streams, GOW field names) leave to
  the module.

Made after the builder check, same day:

- Goldens bind by PNG only; JSON reports are re-blessed in W2 (§3).
- Adopted devices: own command buffers, timeline handoff, dedicated or
  routed queue, 1.3 features required with no fallback (§6).
- Ui requires Gpu; ImGui context and target can be the host's (§4, §8.5).
- CLI verb contract lives in Core; edges only point down (§4, §11).
- Schema is `StructDef`-based with a fixed 2.0 feature set (§5.10).
- `v2` branches from `v1.3.3` (§16).
- `PixelFormat` includes the BC family; `Domain::Text` is `{text, language}`.
- The four `feat/host-ui-hooks` commits are not merged; the command registry
  replaces what they did.
- GoWToolkit is a separate, later project; a new consumer is the first v2 user.
