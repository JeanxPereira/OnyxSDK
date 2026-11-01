# Architecture

This document describes how Onyx is put together as the code stands: the layers and what links what, the purpose of each public contract, the threading and lifetime rules, how errors are reported, and what the SDK leaves out on purpose. The [README](../README.md) is the overview; [`docs/consuming.md`](consuming.md) covers building on Onyx and [`docs/modules.md`](modules.md) covers writing a module. The reasoning behind the design is recorded in the [design spec](superpowers/specs/2026-10-30-onyx-v2-design.md), which was written before the code. Where the spec and this document disagree, this document and the headers describe what ships.

## Layers

Each layer is a static library `Onyx::<Layer>` built from `Source/<Layer>/`. Its public headers live in `Source/<Layer>/include/Onyx/<Layer>/`, and that directory is the only include path the target exports. A header of another layer resolves only through a link to that layer, so an upward include fails to compile instead of being caught by convention.

| Layer | Links publicly | Private dependencies | Built when |
|---|---|---|---|
| `Onyx::Core` | glm | toml++, lz4 | always |
| `Onyx::Cli` | Core | none | always |
| `Onyx::Exchange` | Core | cgltf, stb | always |
| `Onyx::TestKit` | Core | stb, toml++, lz4 | always |
| `Onyx::Gpu` | Core, volk, Vulkan-Headers, VMA | none | `ONYX_WITH_GPU` |
| `Onyx::Render` | Gpu | shaders compiled to SPIR-V at build time | `ONYX_WITH_GPU` |
| `Onyx::Ui` | Render, imgui | none | `ONYX_WITH_UI` |
| `Onyx::Shell` | Ui, GLFW, imgui's GLFW backend | Cli | `ONYX_WITH_SHELL` |
| `Onyx::Media` | Ui | FFmpeg, miniaudio | `ONYX_WITH_MEDIA` |

Every edge points down. Cli, Exchange and TestKit sit beside the graphics stack and depend on Core alone, which is why a command-line tool can link them without Vulkan. Shell links Cli privately to install the stock verbs. Media sits beside Shell: it needs Ui for its view, not the Shell, so a host with its own window can use it too.

Three mechanisms keep the edges honest:

- The include directories above, set by `onyx_add_layer` in `cmake/Layer.cmake`.
- `cmake/StandaloneHeaders.cmake` compiles every public header as the only include of its own translation unit, linked against its layer alone. A header that leans on a missing include, or on a dependency the layer keeps private, fails the build.
- `tools/lint.py` rejects the third-party includes each layer may not name (imgui, volk, VMA, Vulkan, GLFW, `windows.h`, libav, miniaudio, depending on the layer), singleton accessors, and game names in source.

No aggregate `Onyx::Onyx` target exists; a consumer names the layers it uses.

## Contracts

### Files: Vfs

`Vfs::IFile` is a stateless file: a size and `ReadAt(offset, span)`, with no cursor of its own, so any number of readers can share one. `OsFile`, `MemoryFile` and `SliceFile` implement it over a path, a byte vector and a range of a parent file. `Vfs::Reader` is the value type every parser reads through: a shared file, a cursor and endian helpers, so two readers over one file never interfere. `IVirtualFileSystem` lists and opens files inside a mounted archive, and `IsoFileSystem` mounts ISO 9660 images with it. `Vfs::Lz4Decompress` and `Vfs::Lz4FrameDecompress` cover LZ4 blocks and frames, the one compression Core carries.

### Types: TypeCatalog

A type is identified by a key of the form `<module>.<key>` (for example `prm.palette`) and handled at run time as a `Types::TypeId`. `TypeCatalog` holds the `TypeSpec` of every registered type: key, label, `Domain::MediaKind`, icon and colour. A module is never handed the catalog itself. It receives a `TypeRegistrar` that prefixes its own module id, so two modules cannot collide on a key. The catalog belongs to the `Workspace`; there is no process-wide registry.

### Data: Domain

`Onyx::Domain` holds plain value types: `Scene` (mesh parts, materials, textures, an optional `Skeleton` and `AnimationSet`), `Texture`, `Audio` (interleaved float PCM), `Text`, `Table`, and `AssetEntry`, the node of a document's tree. None of them carries a file layout, a GPU layout or a field named after a game. A module converts its format into these types at decode time.

### Modules: IGameModule and DecoderRegistry

`Modules::IGameModule` is the one class a module implements: `Info`, `Probe`, `RegisterTypes`, `RegisterDecoders` and `ParseContainer`, plus `RegisterSettings` and `Mounts`, which have default bodies and are optional. Parsing builds the tree of `AssetEntry` values and decodes nothing.

`Modules::DecoderRegistry` maps a type to its decoders, one slot per payload: `Scene`, `Image`, `Text`, `Audio`, `Table`, `Schema` (a declarative record layout from `Onyx/Core/Schema/Schema.h`) and any number of `Any` slots, each under a capability name. `DecoderRegistry::Of` returns the `Capabilities` of a type, and it is the single routing source: the `decode` verb, the viewer registry and TestKit's decode smoke all consult it. A decoder receives a `DecodeContext` with a reader over the entry's bytes, the document's diagnostics, the settings and the document's module state.

`Onyx/Core/Modules/EntryAccess.h` holds the helpers every consumer of a document needs: `FileOf` and `ReaderOf` for an entry's bytes, `ScopedDocument` to keep a file open for the length of a scope, and `DecodeSceneOf` to decode a scene entry with the workspace settings.

### Documents: Workspace and jobs

`Modules::Workspace` owns the type catalog, the decoder registry, the settings, the `EventBus`, the `JobQueue`, the modules and the open documents. `Open` ranks every module with `RankProbes` and parses on the calling thread; `OpenAsync` runs the same pipeline on the job queue and posts `TreeReady` through `Pump`. A file opens only when the best probe reaches `kProbeFloor` and beats the runner-up, unless a module hint names the module. Modules are added before the first open; afterwards `AddModule` fails.

`Services::JobQueue` is a worker pool with lanes: one job per lane runs at a time, jobs of one lane run in submission order, and different lanes run in parallel. Completion callbacks run only inside `Pump`, on the thread that calls it.

### Settings

`Services::Settings` is a TOML file with typed getters and setters, dirty tracking and an atomic save (a temporary file renamed over the target). A file that fails to parse is copied aside once and the object starts empty, refusing to save until the host clears the error. `Services::SettingsSchema` declares typed keys with defaults and ranges. Each module fills one through `RegisterSettings`; the workspace applies the defaults to its settings, the settings panel renders every schema without knowing the module, and decoders read the values back through `DecodeContext::settings`.

### Command line: VerbTable and exporters

The verb contract lives in Core, so any layer can contribute verbs without depending on `Onyx::Cli`. A `Cli::Verb` is a name, a usage line and a function; a `Cli::VerbTable` holds verbs by name together with a `Cli::Exporters` registry of `decode --to` formats. `Cli::Run` dispatches `argv[1]` to its verb and returns an exit code (`kOk`, `kUsage`, `kNoModule`, `kStrictErrors`, `kPartial`). `Cli::StockVerbs` provides `probe`, `list`, `extract` and `decode`; `Render::CliVerbs` provides `render`; `Exchange::AddExporters` registers `gltf`, `png` and `wav`. The host assembles the table.

A verb that names an entry resolves it with `Cli::ResolveEntry`, which accepts `@<i>/<j>/...` node paths or entry names and returns a `Resolution` whose `ResolveStatus` is `Found`, `NotFound`, `Ambiguous` or `BadPath`. `Cli::ReportResolution` prints the one line a verb writes for a failed resolution, including `cli.ambiguous` with every matching path.

### GPU: Device, own or adopted

`Gpu::Device::Create` makes Onyx's own instance and device. `Gpu::Device::Adopt` wraps a device the host already owns, described by an `ExternalDevice`, and never destroys the host's handles. Both require Vulkan 1.3 with dynamic rendering, synchronization2 and timeline semaphores, and refuse with `gpu.feature` otherwise. The device has one `Gpu::Queue` with a timeline semaphore; every submit returns a `FenceValue` on that timeline. Resources are RAII handles that go to the device's `RetireQueue` when released and are destroyed once the queue passes the value they were last used at.

`FrameRing` records and submits per-frame command buffers. `UploadContext` batches buffer and texture uploads into one submit per `Flush`. Its staging memory is a ring: each flush gets its bytes back when that submission completes, so a full ring waits for the oldest flushed batch instead of draining the queue.

### Rendering: SceneRenderer and RenderToImage

`Render::SceneRenderer` loads a `Domain::Scene` onto the GPU as a `GpuScene` with one upload flush, and records its draw into a frame and a `RenderTarget`. It submits nothing; the owner of the frame does. `Render::RenderToImage` draws a scene once into a fresh target and reads it back as a `Domain::Texture`. The viewport and the headless path share the same `Draw`, so a picture from the `render` verb matches the viewport. Problems in the scene data (bad joint maps, out-of-range indices) are reported as diagnostics and the offending part is skipped.

### UI: UiContext, panels and viewers

`Onyx::Ui` draws into whatever ImGui context is current and never creates one. Every draw receives a `Ui::UiContext`: references to the workspace, theme, command registry, viewer registry, panel host and frame clock, plus pointers to the GPU device, the current frame, the `TexturePool`, the audio output and the fonts. The pointers are null when the host has no GPU device yet or no output, and views must cope with that.

A panel implements `Ui::IPanel` (an id, a title and `Draw`); `Ui::PanelHost` draws the visible ones. A viewer is an `IDocumentView`, a panel bound to one entry. `Ui::ViewerRegistry` routes an entry's type to a `ViewerSpec`, which targets a `MediaKind` or an exact `TypeKey` and may require an `Any` capability. `Ui::OpenEntry` routes the selection, decodes the payload on the job queue and hands the finished view to the caller. A decode that yields nothing is reported on the document as `ui.open.decode`, or as `ui.open.any` when the viewer names an `Any` capability. `Ui::CommandRegistry` holds the commands from which the Shell draws its menu bar and binds its shortcuts.

`Ui::TexturePool` turns `Domain::Texture` values into ImGui texture ids through an `ITextureBinder`, which `ImGuiVulkanBackend` implements. `Create` and `Update` serve textures that change rarely, and `Update` waits for the queue to go idle. `TexturePool::Stream` is the per-frame path: each streamed texture owns a ring of three images and never rewrites a descriptor that a frame in flight uses. It waits on the host only when all three images are still in use or the upload staging ring needs bytes back.

### Application: Shell

`Shell::Shell` is the finished application. Constructing it registers the stock viewers, panels, commands and verbs; no window or GPU object exists until the first `Step`. `Shell::Run` routes the command line through `RouteArgs`: no argument opens the window, a single argument that is not a verb name and names an existing file opens that file in the window, and anything else runs the CLI over `Shell::Verbs`. A host adds modules to `Shell::Workspace`, viewers and panels through an `IShellExtension`, and verbs to `Shell::Verbs`. Only `Cli::StockVerbs` is installed by default; adding `Render::CliVerbs` and `Exchange::AddExporters` is up to the host. `Shell::SetAudioOutput` hands the Shell the output that every audio view shares; a call made during a frame takes effect at the next frame boundary.

### Media: MediaSource

`Media::MediaSource` decodes one clip read from a byte range of an `IFile`, never from a path, so a video stored inside an archive plays without being extracted. `Media::AudioOutput` implements `Ui::IAudioOutput` over miniaudio. `Media::MediaView` is the video viewer and `Media::RegisterMediaViewer` registers it for `MediaKind::Video`. The view uploads its first frame with `TexturePool::Create` and streams every later frame of the same size through `TexturePool::Stream`, so playback never waits for the queue to go idle; the host waits only when all three images of the ring are still in flight or the upload staging ring needs bytes back. A frame of another size removes the texture and creates a new one. No public Media header includes an FFmpeg or miniaudio header.

## Threading

- **Owner thread.** A `Workspace` and everything it owns are confined to one thread, the owner. Job workers touch only the document they parse and a private copy of the settings taken when `OpenAsync` was called. Until the document's `TreeReady` has been pumped, the owner may read only `Document::ready` and the fields `Open` fixed (`id`, `path`, `module`).
- **Main-thread marker.** `Services::MarkMainThread` records the calling thread once, and `ONYX_ASSERT_MAIN_THREAD` asserts against it in debug builds. The SDK states its thread confinement in class comments (`Shell`, for one, is main thread only) and does not call the marker itself; a host that wants the assertion marks its main thread at start-up.
- **Decode lane.** UI decodes run on the job queue lane `Ui::kDecodeLane`, so they never run in parallel with each other. `OpenAsync` uses the document id as its lane, so parses of different documents do run in parallel. `MediaView` submits its open to the decode lane as well and draws a placeholder until the open finishes.
- **Thread-safe pieces.** `DecoderRegistry`, `TypeCatalog` and `DiagSink` are guarded by a mutex, so registration, decoding and reporting may happen on different threads. A decoder that runs off the calling thread must not capture a raw module pointer, because the module can be removed while the decode is in flight. `Settings`, `UploadContext`, `FrameRing`, `SceneRenderer` and `TexturePool` are not thread-safe.
- **Queue mutex.** `Gpu::Queue` serialises `Submit`, `Present` and `WaitIdle` behind one mutex. Code that must use the raw `VkQueue` holds `Queue::Lock`. `ImGuiVulkanBackend` does so around imgui's font uploads, which submit and wait idle on the queue by themselves. A host that adopts its device either gives Onyx a dedicated queue or routes its own submissions through the same mutex. `Device::SubmitCount` counts the submits that went through `Queue::Submit`.
- **Media's decode thread.** Each `MediaSource` creates, uses and destroys every FFmpeg object on one decode thread; the caller only queues commands and reads snapshots. Its clock is the audio clock (frames the device consumed, less the device's latency) when the clip has audio, and a steady clock otherwise. While the audio starves, the thread reads ahead, holding at most 256 compressed packets or 32 MiB, to reach audio stored behind the video. If the audio stays starved for 250 ms with nothing more to read, or ends before the video, the steady clock takes over from the audio position. Decoded frames wait in a mailbox of the newest due frame plus at most four ahead, so a paused source does no work.

There is no render thread. Frames are recorded and submitted on the thread that draws them.

## Errors

Operations that can fail return `Onyx::Result<T>`, which holds either a value or an `Error` with a `code`, a `message` and, for Vulkan failures, the `VkResult` in `native`. Codes have the form `<layer>.<code>`. The upper layers list their codes in the top comment of the header that produces them; Core states each code in the comment of the function that returns it:

| Prefix | Listed in |
|---|---|
| `gpu.` | `Onyx/Gpu/Device.h`, `Onyx/Gpu/Upload.h` |
| `render.` | `Onyx/Render/SceneRenderer.h`, `Onyx/Render/CliVerbs.h` |
| `ui.` | `Onyx/Ui/TexturePool.h`, `Onyx/Ui/Viewers/Opening.h`, `Onyx/Ui/Command.h`, `Onyx/Ui/Fonts.h`, `Onyx/Ui/ImGuiVulkanBackend.h` |
| `shell.` | `Onyx/Shell/Shell.h`, `Onyx/Shell/NativeWindow.h` |
| `media.` | `Onyx/Media/MediaSource.h`, `Onyx/Media/AudioOutput.h` |
| `exchange.` | `Onyx/Exchange/Gltf.h`, `Onyx/Exchange/Png.h`, `Onyx/Exchange/Wav.h` |
| `workspace.`, `settings.`, `lz4.` | `Onyx/Core/Modules/Workspace.h`, `Onyx/Core/Services/Settings.h`, `Onyx/Core/Vfs/Lz4.h` |
| `cli.` | `Onyx/Core/Cli/Resolve.h` |

Problems found in data are not failures of the call. Parsers and decoders report them as `Services::Diag` values (severity, code, message, optional byte position) to the document's `DiagSink` and carry on; an entry that cannot be read is flagged `NodeFlags::Failed` and stays in the tree. A decoder that throws anyway is caught by the registry and reported as `onyx.decode.threw`. Modules use their own prefix for diagnostic codes, as Prism does with `prism.`.

Logging goes through `Services::Log`, an object with sinks and a minimum level. `Log::Default()` is the process-wide instance.

## Lifetimes

Objects that hold GPU resources are destroyed before the objects they were created from. In creation order:

1. The `Gpu::Device`, created or adopted.
2. The objects made from it: `UploadContext`, `FrameRing`, `SceneRenderer` and the `ITextureBinder`. The stock binder, `ImGuiVulkanBackend`, must also be destroyed while its ImGui context is current.
3. The `TexturePool`, which needs the device, the upload context and the binder.
4. The panels and views, which hold texture ids from the pool. `MediaView` removes its texture in its destructor, so the pool must outlive it.

Destruction runs in the reverse order. `Examples/Embedded` declares its objects in this order so that leaving the scope tears them down correctly, and `Shell` does the same internally. The Shell also stops the audio output before any panel is destroyed.

Outside the GPU:

- The `Workspace` outlives every pending job continuation, including the one `OpenEntry` leaves behind.
- An `IAudioOutput` outlives every `UiContext` that points at it. The string `Owner()` returns is valid only until the next `Play` or `Stop`.
- `Workspace::Close` requests cancellation of a parse in flight; the worker keeps the document alive until it finishes.
- `Workspace::RemoveModule` closes the module's documents and unregisters its decoders.

## What is left out

- **Singletons.** No class is reachable through a static `Get()` or `Instance()`, and `tools/lint.py` rejects the pattern. The one process-wide object is the default log. Every other object, catalogs and registries included, is reached through a reference the host holds.
- **A plugin ABI.** Modules are C++ classes linked statically into the host and built with the same compiler. There is no dynamic loading, no C interface and no binary compatibility promise between versions.
- **Game knowledge.** Nothing under `Source/` names a game, and `tools/lint.py` checks it. Format knowledge lives in modules; the two in `Examples/` are synthetic formats made for the SDK's own tests.
- **Write-back.** Onyx reads archives. It does not repack or modify them.
