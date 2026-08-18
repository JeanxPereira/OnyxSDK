# Onyx v1 — Architecture

Onyx is an SDK for building game asset toolkits: applications that open a
game's archives, identify and parse their containers, decode assets into
domain data, and present them in a docked UI, in a headless CLI, or in tests.

The SDK owns everything universal — shell, viewers, rendering, VFS, services,
CLI, test harness. A game enters as a **GameModule** that declares only what
is particular to it: how to recognise its files, which asset types exist, and
how to decode their bytes. The quality bar for the whole design:

> A new toolkit writes no UI code, no render code, and no CLI code.
> It writes format knowledge.

This document defines v1. It supersedes the current (`v0.x`) interfaces; the
port is a break-and-port, not an incremental migration (§12).

---

## 1. Foundation decisions

| Decision | Choice | Rationale |
|---|---|---|
| Consumption model | **Framework** — SDK owns window, dock, lifecycle, CLI | Minimises per-toolkit code; matches how GoWToolkit already works |
| Game integration | **Static linking** — each toolkit links SDK + its modules | No C++ plugin ABI to stabilise; debugging stays trivial |
| Migration | **Break and port** — v1 interfaces are designed clean | One consumer (GoWToolkit), one planned port, no inherited scars |
| Renderer | **Vulkan-native from v1** — no GL port, no dual-backend layer | The current GL renderer becomes the port oracle, then dies |
| Config format | **TOML** for all settings; binary only for regenerable caches | Inspectable, diffable, commentable; GTKC's manual versioning ends |

## 2. Design principle: ready by default, raw by access

Every subsystem exposes two floors, both public:

- **The ready floor** — a complete, opinionated path that covers the common
  case in a few lines: `SceneRenderer` with material roles, mounted VFS,
  the schema engine, stock panels.
- **The raw floor** — the primitives the ready floor is built on:
  `VkDevice`/`VkCommandBuffer` and frame extension points, `IFile` byte
  access, raw ImGui inside a custom panel.

The ready floor never hides the raw floor. If a toolkit outgrows a stock
path, it drops one floor without leaving the SDK.

## 3. Targets and layering

Three layers, enforced as separate CMake targets — an illegal include is a
link error, not a review comment:

```
Onyx::Core     no UI, no GPU. VFS, type identity, asset graph, probe,
               GameModule contracts, domain data (Mesh/Texture/Scene/Anim/
               Audio), schema engine, diagnostics, jobs, settings, logging.
Onyx::Render   Vulkan renderer of domain data. Depends on Core only.
Onyx::Shell    ImGui application framework: window, dock, panels, viewers,
               theme, generic CLI. Depends on Core + Render*.
```

\* Shell's dependency on Render is itself optional (§3.1): a toolkit with no
3D content links a Shell variant without Render.

### 3.1 Optional components

Heavy features are opt-in targets. What an app does not `Use()` is not in
its binary — and not in its dependency fetch:

| Component | Contents | Extra deps |
|---|---|---|
| `Onyx::Viewport3D` | 3D viewer + SceneRenderer wiring | Render layer |
| `Onyx::MediaViewers` | video + audio players | FFmpeg, miniaudio |
| `Onyx::AnimTools` | dopesheet, curves, transport | — |
| `Onyx::Exchange` | exporters (§9): glTF in v1; PNG/WAV in v1.1 | cgltf |
| `Onyx::TestKit` | golden snapshots, decode smoke, render compare | doctest |

```cpp
int main(int argc, char** argv) {
    Onyx::App app("GoWToolkit");
    app.Use(Onyx::Viewport3D());
    app.Use(Onyx::MediaViewers());
    app.Add(Gow2Module());
    app.Add(GowrModule());
    return app.Run(argc, argv);   // GUI without args; generic CLI with args
}
```

A palette-focused COMI toolkit is the same skeleton minus `Viewport3D` and
`MediaViewers`: no GPU layer, no FFmpeg DLLs, a fraction of the binary.

## 4. Type identity

A type is identified by a **namespaced string key** — `"gow2.mesh"`,
`"gowr.texturePair"`, `"comi.room"`. The namespace is the module id and is
applied by construction: a module's registrar is pre-bound to its namespace
and cannot mint outside it.

```cpp
struct TypeKey  { std::string value; };          // persisted identity
struct TypeId   { uint32_t value = 0; };         // runtime handle (interned)

struct TypeSpec {                                 // presentation is data
    std::string   key;          // "mesh" → registered as "<module>.mesh"
    std::string   label;        // "Mesh"
    MediaKind     media;        // Scene | Image | Audio | Video | Text | Blob
    const char*   icon;         // codicon, optional
    float         color[4];
};

class TypeRegistrar {           // handed to IGameModule::RegisterTypes
public:
    TypeId Add(const TypeSpec& spec);            // key gets "<module>." prefix
};
```

Rules:

- Persistence (settings, visibility, layouts) writes the **string key**,
  never the numeric handle. Handles are process-lifetime only.
- The SDK mints a minimal `onyx.*` set (`onyx.unknown`, `onyx.folder`,
  `onyx.raw`). Everything else belongs to a module.
- Collisions are impossible between modules; a duplicate key within one
  module is a registration error.

This retires: the 51 mutable global `TypeId`s, `legacyValue`'s triple role
(array index / runtime handle / persisted value), and the
`gameVer<<8|typeId` packing in asset visibility.

## 5. GameModule

One module = one game (or one family sharing formats). Not a god object: a
manifest of declarations plus a small number of focused entry points.

```cpp
struct ModuleInfo {
    std::string id;             // "gowr" — namespace for types, settings, diags
    std::string displayName;    // "God of War Ragnarök"
    std::vector<std::string> hints;        // CLI: --game gowr|ragnarok
    std::vector<OpenFilter>  openFilters;  // file-picker extensions
};

struct ProbeInput {             // host reads the header ONCE for all modules
    std::filesystem::path path;
    std::span<const std::byte> header;     // first 64 KiB (or whole file)
    uint64_t fileSize;
};
struct ProbeResult {
    int         confidence;     // 0..100
    std::string reason;         // "LZ4 frame magic at 0"
};

class IGameModule {
public:
    virtual ~IGameModule() = default;
    virtual ModuleInfo  Info() const = 0;
    virtual ProbeResult Probe(const ProbeInput&) const = 0;
    virtual void        RegisterTypes(TypeRegistrar&) = 0;
    virtual void        RegisterDecoders(DecoderRegistry&) = 0;
    virtual std::vector<MountSpec> Mounts() const { return {}; }   // §5.2
    virtual ParseResult ParseContainer(ContainerContext&) = 0;
};
```

### 5.1 Detection

The host collects `ProbeResult` from every registered module over the same
pre-read header. Highest confidence wins. A tie, or a best score below the
floor (40), surfaces the full score/reason table — in a dialog (Shell) or on
stderr (`onyx probe <file>`). With a single registered module the probe step
is skipped entirely.

Order of registration carries no semantics. No module references another
module's magic numbers; a module scores its own evidence and nothing else.

### 5.2 Mounts

A module that reads disc images or multi-file archives declares mounts
(`MountSpec { label, extensions, factory → IVirtualFileSystem }`). Modules
whose containers are plain files declare none, and no dead virtuals exist to
stub. The GOW2 module declares an ISO mount; GOWR declares none.

### 5.3 Parsing

```cpp
struct ContainerContext {
    Vfs::IFile&      file;
    Workspace&       workspace;      // settings, services
    DiagSink&        diags;          // §7
    Progress&        progress;       // §8
    ModuleState&     state;          // opaque per-document module storage
};
struct ParseResult {
    AssetTree tree;                  // may be partial — salvage is the norm
};
```

`ParseContainer` performs its own setup internally (the work
`PrepareForParse` used to do — e.g. GOWR's external index configuration now
reads workspace settings, §7.3). Temporal coupling between host calls is
eliminated: there is no function the host must remember to call first.

Module-private parse state (GUID maps, LOD indices) lives in `ModuleState`,
an opaque slot on the document. It never leaks into SDK types.

### 5.4 The asset graph

```cpp
struct AssetNode {
    std::string           name;
    TypeId                type;
    ByteRange             source;      // file + offset + size; may be empty
    std::vector<AssetNode> children;
    NodeFlags             flags;       // e.g. Failed (carries diags)
};
struct AssetTree { AssetNode root; };

class Document {        // one opened container
    // path, module, AssetTree, ModuleState, Diags, mounted VFS (if any)
};
class Workspace {       // composition root — replaces every registry singleton
    // documents, ModuleRegistry, TypeCatalog, DecoderRegistry,
    // Settings, JobQueue
};
```

`Workspace` is constructed by the composition root (Shell app or CLI) and
passed down. Retired singletons: `ProfileManager`, `TypeCatalog::Get`,
`TypeRegistry::Get`, `AssetVisibility::Get`, `AppConfig::Get` (nullable raw
pointer). `Logger` remains global — logging is legitimately process-wide.

## 6. Decoders: capability per type, keyed by output

A module registers decoders per type, keyed by the **domain data** they
produce. Viewers and the CLI ask the registry what a type can become; they
never know which game produced it.

```cpp
class DecoderRegistry {
public:
    void Scene (TypeId, SceneDecoder);    // → Parsers::SceneData
    void Image (TypeId, ImageDecoder);    // → TextureData
    void Audio (TypeId, AudioDecoder);    // → AudioData
    void Text  (TypeId, TextDecoder);     // → string + language tag
    void Schema(TypeId, SchemaRef);       // → declarative layout (§6.1)
};
// every decoder: Result<T> Decode(DecodeContext&) — ctx mirrors
// ContainerContext (file slice, workspace, diags, progress, module state)
```

Consequences:

- The 3D viewport works for **any** module that registers a `Scene` decoder.
  Viewers are paid for once and serve every game.
- Decoders produce **CPU data only**. GPU upload belongs to the renderer on
  the render thread. A decoder that touches the GPU is a bug by definition.
- The current `ITypeHandler` (parse + viewer + scene + icon in one interface)
  is retired: presentation went to `TypeSpec` (§4), behaviour went here.

### 6.1 Declarative schemas

For formats that are regular — header + typed fields + counted arrays — the
layout is data, not code. The schema engine (evolution of the current
`StructDef`/`NodeInstance`) interprets a declaration and provides the
inspector tree, JSON export, and golden-diff support for free:

```cpp
Schema Gowr::InstanceSchema() {
    return Struct("gowr.instance",
        Field<uint64_t>("guid"),
        Field<Vec3>("position"),
        ArrayStruct("overrides", OverrideSchema(), CountAt(0x14)));
}
```

Formats that are algorithms — variable vertex-stream semantics, external
LOD packs, compression — are code decoders. The common hybrid: a code
decoder for the heavy payload, a schema for its regular header, so even the
hard formats have their structured part visible in the inspector. Both
floors are first-class; the module chooses per type.

## 7. Diagnostics, jobs, settings

### 7.1 Diagnostics are data

```cpp
enum class Severity { Info, Warning, Error };
struct Diag {
    Severity    severity;
    std::string code;        // namespaced: "gowr.mesh.lod-missing"
    std::string message;
    std::optional<ByteRef> at;   // file + offset, when known
};
class DiagSink { public: void Report(Diag); };
```

Policy: **salvage by default**. In a reverse-engineering toolkit, a
half-understood or half-corrupt file is the normal case. A failed node is
marked `Failed` in the tree with its diags attached; the rest of the
document stays navigable. Exceptions do not cross the module boundary — a
module reports and returns what it has. Fail-fast exists as a CLI switch
(`--strict`: exit non-zero on any Error).

Surfaces: a Problems panel in the Shell, diags embedded in CLI `--report`
JSON, and per-node display in the tree browser.

### 7.2 Jobs

Document-level concurrency only — deliberately not a job system:

- `ParseContainer` and decoders run on a worker thread, one call at a time
  per document. Module code is written single-threaded.
- `Progress& { void Step(float, std::string_view label); }` and cooperative
  `ctx.CancelRequested()`.
- The Shell renders progress and wires cancellation; modules never touch UI.

### 7.3 Settings

TOML, three scopes, namespaced keys:

| Scope | Examples | Lives in |
|---|---|---|
| Application | theme, layout, recent files | user config dir |
| Module | `[gowr] texIndexDir = "..."` | user config dir, per module id |
| Workspace | per-game-folder state (what GOWR's `config.ini` holds today) | beside the data or in workspace store |

The SDK owns load/save/watch; modules read typed values through
`workspace.settings`. Binary formats survive only as regenerable caches
(e.g. LOD pack index), never as the source of truth. GTKC is retired; the
port ships a one-way GTKC→TOML importer.

### 7.4 Events

A typed event bus decouples the Shell's panels from the Workspace and from
each other. Its governing rule: **an event is a signal, not a data
transport**. An event says *what changed* (by id); consumers pull current
state from the Workspace. The bus never becomes a second data model.

```cpp
class EventBus {                       // member of Workspace — not global
public:
    template<class E> Subscription On(std::function<void(const E&)>);
    template<class E> void Post(E);    // queued; dispatched FIFO on the main thread
};
```

Mechanics — the three decisions that prevent the classic event-bus bugs:

- **Owned by the Workspace**, not a singleton. A headless CLI Workspace has
  a bus with no subscribers at zero cost; multiple workspaces cannot
  crosstalk.
- **`Subscription` is RAII** — destruction unsubscribes. A closed panel
  cannot leave a dangling listener.
- **Payloads are ids and values** (`DocumentId`, `NodePath`, `TypeKey`) —
  never pointers into containers. Posting from worker threads is safe;
  dispatch is FIFO on the main thread, so ordering is deterministic and
  handlers never re-enter mid-draw.

Core event set: `DocumentOpened/Closed`, `TreeReady`, `DiagsAdded`,
`SelectionChanged`, `JobStarted/Finished`, `SettingChanged`.
`SelectionChanged` is the extensibility workhorse: a toolkit's custom panel
(e.g. a COMI palette panel) subscribes, checks the selected node's type, and
reacts — no SDK hooks required.

Where events are forbidden: inside parse/decode (modules stay synchronous
and deterministic — `DiagSink&`/`Progress&` are direct interfaces, with
guaranteed order, usable without any bus), inside the render frame (the
frame is an explicit graph), and as request/response RPC.

### 7.5 Ownership (RAII) and threading

**Ownership rules.** Every resource in the SDK is owned by an RAII type; a
naked handle exists only on the raw floor, and even there the SDK hands out
*views* of resources it still owns.

- **CPU:** domain data is value types under `unique_ptr`/containers; files
  are `shared_ptr<IFile>`; subscriptions (`Subscription`) and jobs
  (`JobHandle`) unsubscribe/detach in their destructors.
- **GPU:** every Vulkan object lives in a `Unique<T>` wrapper backed by VMA.
  Destruction is **deferred by fence**: a wrapper's destructor enqueues the
  handle on the current frame's retire list; the actual `vkDestroy*` runs
  when that frame's fence signals. Code can drop resources at any time
  without knowing what the GPU is still reading — the "resources must die
  while the context is current" class of bug (present in the GL code today)
  is retired by construction.
- **Shutdown is destruction order.** No resource-owning singletons: the
  composition root (Shell app or CLI) constructs Workspace → RenderContext →
  panels explicitly, so teardown is the exact reverse and deterministic.
  The current pattern — `ShaderManager::Get()` statics racing the GL
  context at exit — cannot be expressed in v1.

**Thread manifest.** v1 has exactly two kinds of threads, and every API
states which one it belongs to:

| Thread | Owns | Rules |
|---|---|---|
| **Main** | OS events, ImGui, event-bus dispatch, all Shell state, **all Vulkan submission** | UI and render APIs assert main-thread (debug builds), extending the existing `Threading::MarkMainThread` |
| **Worker pool** | `ParseContainer` and decoder calls, one in-flight call per document | Module code is single-threaded per call and touches only its `ContainerContext` slice, never the Workspace |

A dedicated render thread is deliberately **not** part of v1 — an asset
viewer does not need one, and the complexity is real. The design leaves the
door open (submission is already centralised), but the door stays closed
until profiling opens it.

**Crossing threads.** Ownership crosses, references do not:

- A decoder returns `unique_ptr<SceneData>` and keeps nothing; results are
  *moved* to the main thread. No shared mutable state exists outside the
  internally-locked job and event queues.
- `DiagSink` and `Progress` are thread-safe by contract: workers write,
  buffers drain on the main thread.
- Completion callbacks are posted to the main thread and identified by
  `DocumentId` + generation — if the document died meanwhile, the callback
  no-ops. `JobHandle::Cancel()` is explicit and cooperative; its destructor
  detaches without blocking (a blocking destructor on the main thread is a
  UI hang by definition).
- Decoders never touch the GPU (§6) — uploads happen on the main thread
  from moved CPU data.

**Verification:** the Linux CI job runs Core's job/event tests under TSAN;
Windows runs the same tests without it. Main-thread asserts are active in
all debug builds.

## 8. Rendering: Vulkan-native

`Onyx::Render` is written directly against Vulkan 1.3 (dynamic rendering,
VMA for allocation). There is **no backend abstraction layer** — a dual
GL/Vulkan indirection would cost both APIs' complexity and deliver neither's
power. The existing GL renderer is not ported; it serves as the **oracle**
(§10) until the parity gate passes, then it is deleted.

Two floors (§2):

- **Ready:** `SceneRenderer` — takes `SceneData`, renders PBR with material
  roles, skinning, camera, grid, skeleton overlay. Offscreen-first: the
  window swapchain is just one more render target; headless CLI rendering
  is the same code path, not a variant.
- **Raw:** `RenderContext` exposes `VkDevice`, `VkQueue`, the frame's
  `VkCommandBuffer`, and `AddPass(...)` extension points where an app
  records its own commands. No translation layer between the app and
  Vulkan.

### 8.1 Material model

Positional texture layers plus the `pbrLayers` flag are retired. A material
carries **explicit roles**:

```cpp
enum class TextureRole { Diffuse, Normal, Occlusion, Gloss, Height,
                         Scatter, Detail, Emissive, EnvMap };
struct MaterialDesc {
    float baseColor[4];
    BlendMode blend;
    std::map<TextureRole, TextureRef> textures;   // sparse
};
```

A GOW2 material fills one or two roles; a GOWR material fills seven; the
renderer never knows which game filled them.

## 9. Exchange

Exporters mirror decoders — domain data out to interchange formats.

- **v1: glTF export of `SceneData`** (cgltf). This is deliberate: glTF
  opened in Blender is an *external* oracle for skinning, weights and
  tangents — validation not written by us, independent of our renderer.
- v1.1: PNG (`TextureData`), WAV (`AudioData`), and CLI
  `decode --to <format>` wiring for all of them.

## 10. Testing: TestKit and the oracle strategy

`Onyx::TestKit` ships with the SDK so every toolkit is born testable:

- **Tree goldens** — snapshot an `AssetTree` (names, keys, sizes, payload
  hashes) to JSON; diff on regression. (Ports GoWToolkit's golden helpers.)
- **Decode smoke** — walk a container, decode everything decodable, assert
  zero `Error` diags (or a recorded allowlist).
- **Render compare** — render `SceneData` headless, compare PNG against a
  reference with tolerance (`maxChannelDelta`, `%differingPixels`) to absorb
  driver variance.

Oracle strategy for the Vulkan port: the current GL renderer, driven by the
GoWToolkit headless harness, produces the reference corpus (fixed set of
scenes, four canonical views each). The Vulkan renderer must match within
tolerance before GL is deleted. The same comparator then runs in CI on
**lavapipe** (Mesa's software Vulkan), so PBR, shaders, texturing and
lighting stay under automatic watch on machines with no GPU.

Fixture policy: tiny synthetic fixtures live in the repo (a skinned cube, a
16 px BC-compressed texture); real-game fixtures are referenced by env var
and tests skip cleanly when absent. Commercial game data never enters the
repository.

## 11. Shell and the generic CLI

The Shell owns window, dock, native menus, theme, layout persistence, and
the stock panels: tree browser, inspector (schema-driven), problems, log,
settings. All of them operate on `Workspace` + `TypeSpec` metadata — no
game code runs to draw a tree row.

Custom UI is a public contract, not an escape hatch: `IPanel`,
`IDocumentContent` and the widget library are exported (raw ImGui remains
reachable — §2). A COMI palette panel is a first-class citizen written
against the same widgets the SDK uses.

`App::Run(argc, argv)` embeds the generic CLI — every toolkit gets:

```
probe <file>              score/reason table from every module
list <file> [--json]      parsed tree
extract <file> <dir>      raw payloads
decode <file> <entry>     domain data → report (later: --to gltf)
render <file> <entry>     offscreen PNG + per-batch JSON report
                          [--views iso,front,...] [--strict]
```

GUI and CLI share one `Workspace` code path; a headless result and an
on-screen result can only diverge if something real diverges.

## 12. Port plan — five waves, each buildable, each gated

| Wave | Content | Gate |
|---|---|---|
| **W1 Targets** | Split Core/Render(GL, as-is)/Shell; optional-component skeleton; illegal includes fixed (imgui out of Services/Rendering; Viewers↔App cycle broken) | Everything builds; GoWToolkit runs on the split targets |
| **W2 Identity & state** | TypeKey/TypeSpec/TypeRegistrar; TOML settings + GTKC importer; diagnostics + jobs plumbing in Core | Old configs import; goldens unchanged |
| **W3 Modules** | IGameModule, probe, DecoderRegistry, Workspace; port gow2 + gowr modules; retire IAssetProfile/ITypeHandler/singletons; generic CLI absorbs the GoWToolkit harness | GoWToolkit fully functional on v1 contracts; CLI render matches pre-port oracle images |
| **W4 Vulkan** | Onyx::Render rewritten on Vulkan 1.3; SceneRenderer with role materials; oracle gate vs GL corpus; lavapipe in CI; delete GL | Corpus matches within tolerance on hardware and lavapipe; GL sources removed |
| **W5 Generality** | Toy `comi` module (probe LA0, tree, one image decoder, palette panel); TestKit extraction; glTF export; v1.0 tag | COMI toolkit builds with zero SDK edits; TestKit green in CI |

W2 is where something always bleeds (persisted-format migration); its
importer is one-way by design — reads old, writes new, never writes old.

## 13. The generality test

The exit criterion for v1 is not "GoWToolkit works again" — it is that a
second, unrelated toolkit exists with **zero SDK edits**. The COMI toy
module (W5) is that proof. During any wave, if a Core change needs
`if (gowr)` — or any game name — it is an architecture bug and goes back to
the drawing board, not into the tree.

## 14. Out of scope for v1 (recorded backlog)

- Viewport UX rebuild (skeleton toggles, visualisation controls, gizmos) —
  explicitly after v1, on top of the Vulkan viewport.
- Runtime plugin loading (DLL modules). Static-only in v1; interfaces avoid
  gratuitous ABI hostility but no ABI promise is made.
- Write-back/repack of game archives.
- PNG/WAV export, `decode --to` for non-glTF targets (v1.1).

## 15. Stability policy

- Pre-1.0: a minor bump may break API; the changelog says so explicitly.
- Post-1.0: SemVer. Deprecations ship with `[[deprecated]]` and survive one
  minor before removal.
- Public surface = `Include/Onyx/**` except `Include/Onyx/Detail/**`.
- API stability only — no ABI promise (static linking; consumers recompile).

## 16. Risks

| Risk | Mitigation |
|---|---|
| W4 (Vulkan) is ~⅓ of total effort and can stall the tail | It is last; W1–W3 deliver value on the GL renderer unchanged. The oracle corpus is produced **before** W4 starts |
| GTKC→TOML migration loses user state | One-way importer written against real config files from both machines before the old writer is deleted |
| Salvage policy hides real regressions | `--strict` in CI decode-smoke: new Error diags fail the build unless allowlisted |
| Layer split reveals hidden couplings beyond the known ones | W1 is scoped to *moving and cutting includes only* — no behaviour change, so every breakage is a coupling being surfaced, not a feature bug |
| lavapipe vs hardware divergence flakes CI | Tolerance comparator (maxChannelDelta + %pixels) tuned on the oracle corpus; hardware run remains the authority |
