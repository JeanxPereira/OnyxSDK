# Onyx v1 — M3a: Module Contracts, Workspace and Headless CLI

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The GameModule contract (evidence-based probe, container parsing,
decoders keyed by output), the Workspace composition root, role-based
materials, and the generic headless CLI — proven end-to-end by a synthetic
example module, without touching the Shell.

**Architecture:** Everything lands in `Onyx::Core` (plus one contained
change to `Onyx::Render` for material roles). The legacy path
(IAssetProfile/ProfileManager/AssetDatabase) stays alive and untouched —
the Shell keeps running on it until M3b rewires it and deletes it. The
example module `onyxbox` (Examples/OnyxBox) is the gate: probe → open →
tree → decode → CLI, all through the new contracts only.

**Tech Stack:** C++20, doctest, toml++ (in), CMake/Ninja/MSVC.

**Spec:** `docs/design/2026-08-18-onyx-v1-architecture.md` §5 (GameModule),
§5.1 (detection), §5.3 (parsing), §5.4 (asset graph), §6 (decoders),
§7.1/7.2 (diag/jobs wiring), §8.1 (material roles), §11 (CLI).

## Global Constraints

- **Repo:** `E:\CodingProjects\OnyxSDK`, branch `feat/onyx-v1-m3a` (create
  from main at start). Commits: Conventional, sentence-case subject,
  explicitly staged paths, **NO AI attribution of any kind** — hard rule.
- **Build:** the MSVC-configured dir `E:\CodingProjects\OnyxSDK\build`.
  Do NOT reconfigure or delete it; a configure failure = STOP and report.
  `cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul && cd /d E:\CodingProjects\OnyxSDK\build && cmake .. && ninja && ctest --output-on-failure'`
- **Layering:** all new code is CORE (no imgui/GLFW/glad) except Task 5's
  `Source/Rendering/SceneRenderer.cpp` change (Render layer, glad allowed
  there). New Core sources go in `ONYX_CORE_SOURCES`; the configure-time
  completeness check enforces membership; LayerGuard enforces purity.
- **Legacy untouched:** `IAssetProfile.h`, `ProfileManager.*`,
  `AssetDatabase.*` are not modified or deleted in M3a (M3b's job). The
  one exception is additive: `AssetEntry` gains a `flags` field.
- **Consumers:** GoWToolkit is pinned to tag v0.6.0 and unaffected; the
  only SDK-internal SceneData producers/consumers are SceneRenderer,
  Viewport3D and MinimalViewer — Task 5 keeps them compiling.
- Existing suite is 25 ctest entries; every task ends full-suite green
  plus its own additions.
- Established interfaces from M2 you will consume (all in `Include/Onyx/`):
  `Types::TypeRegistrar(TypeCatalog&, moduleId)` + `TypeId Add(const TypeInfo&)`;
  `Types::TypeCatalog::{Find,KeyOf,Count,Register}`;
  `Services::DiagSink::{Report,Drain,Count,HasErrors}` (thread-safe);
  `Services::Progress::{Step,CancelRequested,Peek}`;
  `Services::JobQueue::{Submit(lane,work,done),Pump,PendingCallbacks}`;
  `Services::EventBus::{On<E>,Post<E>,Pump}` + RAII `Subscription`;
  `Services::Settings::{Load,Save,GetX,Set,Dirty,Path}`.

---

### Task 1: GameModule contract and evidence-based probe

**Files:**
- Create: `Include/Onyx/Modules/GameModule.h`
- Create: `Include/Onyx/Modules/Probe.h`
- Create: `Source/Modules/Probe.cpp`
- Modify: `CMakeLists.txt` (ONYX_CORE_SOURCES += Source/Modules/Probe.cpp)
- Test: `Tests/probe_test.cpp` (ctest name OnyxProbe, registered like
  settings_test.cpp)

**Interfaces:**
- Produces (verbatim):

```cpp
// Include/Onyx/Modules/GameModule.h
#pragma once
#include <Onyx/Types/TypeRegistrar.h>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Onyx::Vfs { class IVirtualFileSystem; }
namespace Onyx::Modules {

struct OpenFilter {
    std::string label;                    // "God of War Ragnarok"
    std::vector<std::string> extensions;  // lowercase, no dot: {"wad"}
};

struct ModuleInfo {
    std::string id;            // "gowr" — namespace for types/settings/diags
    std::string displayName;   // "God of War Ragnarok"
    std::vector<std::string> hints;       // CLI: --game gowr|ragnarok
    std::vector<OpenFilter>  openFilters;
};

struct ProbeInput {
    std::filesystem::path path;
    std::span<const std::byte> header;   // first 64 KiB, pre-read by the host
    uint64_t fileSize = 0;
};

struct ProbeResult {
    int         confidence = 0;   // 0..100
    std::string reason;           // "LZ4 frame magic at 0"
};

struct MountSpec {
    std::string label;
    std::vector<std::string> extensions;  // lowercase, no dot
    // Factory returns nullptr when the file cannot be mounted after all.
    std::function<std::shared_ptr<Vfs::IVirtualFileSystem>(
        const std::filesystem::path&)> mount;
};

class DecoderRegistry;    // Task 3
struct ContainerContext;  // Task 2

struct ParseResult {
    bool ok = false;      // false = nothing usable (tree may still be partial)
};

class IGameModule {
public:
    virtual ~IGameModule() = default;
    virtual ModuleInfo  Info() const = 0;
    virtual ProbeResult Probe(const ProbeInput&) const = 0;
    virtual void        RegisterTypes(Types::TypeRegistrar&) = 0;
    virtual void        RegisterDecoders(DecoderRegistry&) = 0;
    virtual std::vector<MountSpec> Mounts() const { return {}; }
    virtual ParseResult ParseContainer(ContainerContext&) = 0;
};

} // namespace Onyx::Modules
```

```cpp
// Include/Onyx/Modules/Probe.h
#pragma once
#include <Onyx/Modules/GameModule.h>

namespace Onyx::Modules {

inline constexpr int kProbeFloor = 40;   // best score below this => nobody wins

struct ProbeRanking {
    struct Row { IGameModule* module; ProbeResult result; };
    std::vector<Row> rows;        // sorted by confidence, descending; ties keep
                                  // registration order (stable sort)
    // Winner: the single top row when its confidence >= kProbeFloor AND it
    // is strictly greater than the runner-up (a tie means ambiguity).
    IGameModule* winner = nullptr;
};

// Reads the header once and asks every module. Never throws; an unreadable
// file yields an empty ranking (no winner).
ProbeRanking RankProbes(const std::vector<IGameModule*>& modules,
                        const std::filesystem::path& file);

// Same, for callers that already hold the bytes (tests, archives).
ProbeRanking RankProbes(const std::vector<IGameModule*>& modules,
                        const ProbeInput& input);
```

- [ ] **Step 1: Write the failing tests**

```cpp
#include <doctest/doctest.h>
#include <Onyx/Modules/Probe.h>
using namespace Onyx::Modules;

namespace {
// Minimal fake: fixed score, records nothing.
struct FakeModule : IGameModule {
    std::string id; int score; std::string why;
    FakeModule(std::string i, int s, std::string w) : id(i), score(s), why(w) {}
    ModuleInfo  Info() const override { return {id, id, {}, {}}; }
    ProbeResult Probe(const ProbeInput&) const override { return {score, why}; }
    void RegisterTypes(Onyx::Types::TypeRegistrar&) override {}
    void RegisterDecoders(DecoderRegistry&) override {}
    ParseResult ParseContainer(ContainerContext&) override { return {}; }
};
} // namespace

TEST_CASE("RankProbes picks the highest scorer above the floor") {
    FakeModule a("a", 20, "weak"), b("b", 95, "magic"), c("c", 60, "plausible");
    ProbeInput in{}; // empty header is fine for fakes
    auto r = RankProbes({&a, &b, &c}, in);
    REQUIRE(r.rows.size() == 3);
    CHECK(r.rows[0].result.confidence == 95);
    CHECK(r.winner == &b);
}

TEST_CASE("A tie at the top means no winner") {
    FakeModule a("a", 80, "x"), b("b", 80, "y");
    auto r = RankProbes({&a, &b}, ProbeInput{});
    CHECK(r.winner == nullptr);
    CHECK(r.rows.size() == 2);
}

TEST_CASE("Best score below the floor means no winner") {
    FakeModule a("a", 39, "meh");
    auto r = RankProbes({&a}, ProbeInput{});
    CHECK(r.winner == nullptr);
}

TEST_CASE("File-path overload reads the header once and feeds every module") {
    // Module that scores by inspecting the header bytes.
    struct SniffModule : FakeModule {
        using FakeModule::FakeModule;
        ProbeResult Probe(const ProbeInput& in) const override {
            if (in.header.size() >= 4 &&
                std::memcmp(in.header.data(), "OBX1", 4) == 0)
                return {95, "OBX1 magic"};
            return {0, "no magic"};
        }
    };
    auto tmp = std::filesystem::temp_directory_path() / "onyx_probe_test.bin";
    { std::ofstream f(tmp, std::ios::binary); f << "OBX1rest-of-file"; }
    SniffModule s("s", 0, "");
    FakeModule  other("o", 10, "ext only");
    auto r = RankProbes(std::vector<IGameModule*>{&s, &other}, tmp);
    CHECK(r.winner == &s);
    std::filesystem::remove(tmp);
}

TEST_CASE("An unreadable path yields an empty ranking") {
    FakeModule a("a", 95, "x");
    auto r = RankProbes(std::vector<IGameModule*>{&a},
                        std::filesystem::path("Z:/does/not/exist.bin"));
    CHECK(r.rows.empty());
    CHECK(r.winner == nullptr);
}
```

- [ ] **Step 2: Run, expect compile failure**
- [ ] **Step 3: Implement** — `Probe.cpp`: the path overload opens the file,
  reads `min(64*1024, size)` bytes into a local buffer, builds `ProbeInput`
  and delegates. Ranking: collect rows, `std::stable_sort` by confidence
  desc, winner per the header's rule. Clamp each module's confidence to
  [0,100] defensively.
- [ ] **Step 4: Build + full ctest green**
- [ ] **Step 5: Commit** — `feat(modules): IGameModule contract and evidence-ranked probe`

### Task 2: Workspace and Document

**Files:**
- Create: `Include/Onyx/Modules/Workspace.h`
- Create: `Source/Modules/Workspace.cpp`
- Modify: `Include/Onyx/Domain/Entry.h` (additive: `flags`)
- Modify: `CMakeLists.txt` (ONYX_CORE_SOURCES += Workspace.cpp)
- Test: `Tests/workspace_test.cpp` (ctest OnyxWorkspace)

**Interfaces:**
- Consumes: Task 1's contracts; M2's EventBus/JobQueue/DiagSink/Settings/
  TypeRegistrar.
- Produces (verbatim):

```cpp
// Additive on Onyx::Domain::AssetEntry (Entry.h):
enum class NodeFlags : uint8_t { None = 0, Failed = 1 };
// inside AssetEntry:
NodeFlags flags = NodeFlags::None;

// Include/Onyx/Modules/Workspace.h
#pragma once
#include <Onyx/Modules/GameModule.h>
#include <Onyx/Modules/Probe.h>
#include <Onyx/Services/Diagnostics.h>
#include <Onyx/Services/EventBus.h>
#include <Onyx/Services/Jobs.h>
#include <Onyx/Services/Settings.h>
#include <Onyx/Types/TypeCatalog.h>
#include <Onyx/Domain/Entry.h>
#include <Onyx/Vfs/IFile.h>

namespace Onyx::Modules {

using DocumentId = uint64_t;   // 0 = invalid

// Per-document opaque module storage (spec §5.3). The module downcasts.
using ModuleState = std::shared_ptr<void>;

struct ContainerContext {
    Vfs::IFile&            file;
    Services::Settings&    settings;      // workspace-scope settings
    Services::DiagSink&    diags;
    Services::Progress&    progress;
    ModuleState&           state;         // module writes what it wants kept
    std::vector<Domain::AssetEntry>& roots;   // module fills the tree here
};

struct Document {
    DocumentId  id = 0;
    std::filesystem::path path;
    IGameModule* module = nullptr;        // owned by the Workspace
    std::vector<Domain::AssetEntry> roots;
    std::shared_ptr<Vfs::IFile> file;
    Services::DiagSink diags;
    ModuleState state;
    bool ready = false;                   // true once parse finished (ok or not)
};

// Events (id payloads only, spec §7.4):
struct DocumentOpened { DocumentId id; };
struct TreeReady      { DocumentId id; bool ok; };
struct DocumentClosed { DocumentId id; };

class Workspace {
public:
    // The catalog is handed in (spec §7.5 — no singleton reach). The
    // composition root passes TypeCatalog::Get() until Shell rework.
    explicit Workspace(Types::TypeCatalog& catalog);
    ~Workspace();                         // joins jobs (JobQueue teardown)

    // Registers the module's types (namespaced by Info().id) and decoders.
    void AddModule(std::unique_ptr<IGameModule> m);
    const std::vector<std::unique_ptr<IGameModule>>& Modules() const;
    IGameModule* FindModule(std::string_view idOrHint) const;   // id, then hints

    ProbeRanking Probe(const std::filesystem::path&) const;

    // Synchronous open: probe (or forced module), parse on the caller's
    // thread. Returns 0 and reports into the document's DiagSink on failure
    // stages that still produce a document; returns 0 with no document only
    // when no module accepts the file.
    DocumentId Open(const std::filesystem::path&, std::string_view moduleHint = {});

    // Async: same pipeline on the JobQueue (lane = document id). TreeReady
    // is posted on the bus; callers Pump() the queue and the bus.
    DocumentId OpenAsync(const std::filesystem::path&, std::string_view moduleHint = {});

    Document*   Get(DocumentId);
    void        Close(DocumentId);        // posts DocumentClosed

    Services::EventBus& Events();
    Services::JobQueue& Jobs();
    Services::Settings& WorkspaceSettings();
    class DecoderRegistry& Decoders();    // defined in Task 3
    Types::TypeCatalog&  Catalog();
};

} // namespace Onyx::Modules
```

Notes that bind the implementation:
- `Open` never throws and never aborts the whole document for a partial
  parse: the module reports diags and returns what it has (salvage, §7.1).
- Document ids are monotonically increasing, never reused (generation
  safety for late callbacks).
- `OpenAsync`'s completion callback (posted via JobQueue::Pump) must no-op
  if the document was Closed meanwhile (look up by id, not pointer).
- Workspace owns: EventBus, JobQueue(2 workers), workspace Settings
  (constructed empty at a temp-neutral path via `Settings::Load` on
  `path/.onyx/workspace.toml` only when a real path is opened — for M3a,
  construct with `Settings::Load(std::filesystem::path{})` equivalent:
  give Workspace a `SetWorkspaceSettingsPath(path)`; default is an
  in-memory empty Settings whose Save() is a no-op returning false).
  Keep this minimal — full scope wiring is M3b.

- [ ] **Step 1: Failing tests** — using a stateful fake module:

```cpp
// Fake that parses a "tree" of N entries from the first byte of the file,
// marks entry index 1 Failed with a diag, and stashes ModuleState.
struct BoxFake : Onyx::Modules::IGameModule {
    // Info: id "boxfake", hint "bf". Probe: header[0]=='B' -> 90 else 0.
    // ParseContainer: reads byte 1 = count; emits count entries named
    // "e0".."eN-1" (typeId from RegisterTypes minting "boxfake.item");
    // entry 1 (when present) gets flags=Failed plus a diag
    // {Error, "boxfake.item.bad", "entry 1 corrupt"}; state = count.
};

TEST_CASE("Workspace opens a document through probe and salvages") {
    auto tmp = write_temp_file("B\x03rest");     // 'B' magic, count 3
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(std::make_unique<BoxFake>());
    int opened = 0; Onyx::Modules::DocumentId readyId = 0;
    auto s1 = ws.Events().On<Onyx::Modules::DocumentOpened>([&](auto& e){ ++opened; });
    auto s2 = ws.Events().On<Onyx::Modules::TreeReady>([&](auto& e){ readyId = e.id; });
    auto id = ws.Open(tmp);
    REQUIRE(id != 0);
    ws.Events().Pump();
    CHECK(opened == 1); CHECK(readyId == id);
    auto* doc = ws.Get(id);
    REQUIRE(doc); REQUIRE(doc->roots.size() == 3);
    CHECK(doc->roots[1].flags == Onyx::Domain::NodeFlags::Failed);
    CHECK(doc->diags.HasErrors());               // salvage: tree + diags
    CHECK(doc->roots[0].flags == Onyx::Domain::NodeFlags::None);
}

TEST_CASE("No module accepts the file: Open returns 0") { /* header 'X' */ }

TEST_CASE("OpenAsync delivers TreeReady through the pumps") {
    // OpenAsync, then loop: ws.Jobs().Pump(); ws.Events().Pump();
    // until readyId != 0 (with a deadline ~2s). Assert doc->ready.
}

TEST_CASE("Close posts DocumentClosed and Get returns null after") { ... }

TEST_CASE("FindModule resolves by id then hint") { /* "boxfake", "bf" */ }
```

(Write the elided cases in full — each asserts the named behavior.)

- [ ] **Step 2: Run, expect failure**
- [ ] **Step 3: Implement** (Workspace.cpp; the parse pipeline shared by
  Open/OpenAsync is one private function producing into a Document).
- [ ] **Step 4: Build + full ctest green**
- [ ] **Step 5: Commit** — `feat(modules): Workspace and Document - the composition root`

### Task 3: DecoderRegistry keyed by output

**Files:**
- Create: `Include/Onyx/Modules/DecoderRegistry.h`
- Create: `Source/Modules/DecoderRegistry.cpp`
- Modify: `Include/Onyx/Modules/Workspace.h` + `Source/Modules/Workspace.cpp`
  (own a DecoderRegistry; AddModule calls RegisterDecoders)
- Modify: `CMakeLists.txt`
- Test: `Tests/decoderregistry_test.cpp` (ctest OnyxDecoders)

**Interfaces:**

```cpp
// Include/Onyx/Modules/DecoderRegistry.h
#pragma once
#include <Onyx/Modules/Workspace.h>      // DecodeContext needs Document bits
#include <Onyx/Parsers/SceneNode.h>
#include <Onyx/Parsers/TextureData.h>
#include <Onyx/Types/TypeId.h>
#include <functional>
#include <memory>
#include <optional>

namespace Onyx::Modules {

struct DecodeContext {
    Document&              doc;           // tree, file, module state
    const Domain::AssetEntry& entry;      // the node being decoded
    Services::DiagSink&    diags;
    Services::Progress&    progress;
};

struct TextOut { std::string text; std::string language; };  // language: "json", "lua", ""

using SceneDecoder = std::function<std::unique_ptr<Parsers::SceneData>(DecodeContext&)>;
using ImageDecoder = std::function<std::unique_ptr<Parsers::TextureData>(DecodeContext&)>;
using TextDecoder  = std::function<std::optional<TextOut>(DecodeContext&)>;

class DecoderRegistry {
public:
    void Scene(Types::TypeId, SceneDecoder);
    void Image(Types::TypeId, ImageDecoder);
    void Text (Types::TypeId, TextDecoder);

    bool HasScene(Types::TypeId) const;
    bool HasImage(Types::TypeId) const;
    bool HasText (Types::TypeId) const;

    // Null/empty result when no decoder or the decoder salvage-fails
    // (failure detail goes through ctx.diags, never an exception).
    std::unique_ptr<Parsers::SceneData>   DecodeScene(DecodeContext&) const;
    std::unique_ptr<Parsers::TextureData> DecodeImage(DecodeContext&) const;
    std::optional<TextOut>                DecodeText (DecodeContext&) const;
};

} // namespace Onyx::Modules
```

Rules: a decoder that throws is contained here (try/catch + diag
`{Error, "<typekey>.decoder-threw", what()}` + null return) — the same
§7.1 boundary the JobQueue and EventBus enforce. Audio/Schema slots are
deliberately absent until a consumer exists (YAGNI; the header carries a
one-line comment saying so).

- [ ] **Step 1: Failing tests** — register fake decoders on two minted
  types; assert Has*/Decode* dispatch by TypeId; assert the wrong-type
  lookup returns null without touching other registrations; assert a
  throwing decoder yields null + one Error diag with the code
  `"<key>.decoder-threw"`; assert double registration on the same TypeId
  replaces (last wins) — document that in the header.
- [ ] **Step 2: Run, expect failure**
- [ ] **Step 3: Implement** (three unordered_maps keyed by TypeId.value).
- [ ] **Step 4: Build + full ctest green**
- [ ] **Step 5: Commit** — `feat(modules): DecoderRegistry - capabilities keyed by output`

### Task 4: The onyxbox example module, end to end

**Files:**
- Create: `Examples/OnyxBox/OnyxBoxModule.h`
- Create: `Examples/OnyxBox/OnyxBoxModule.cpp`
- Create: `Examples/OnyxBox/CMakeLists.txt` (STATIC lib `Onyx_ExampleBox`,
  links Onyx_Core, added via `add_subdirectory(Examples/OnyxBox)` from the
  root — unconditionally, it is tiny and the tests need it)
- Modify: root `CMakeLists.txt` (add_subdirectory), `Tests/CMakeLists.txt`
  (link Onyx_ExampleBox)
- Test: `Tests/onyxbox_test.cpp` (ctest OnyxBox)

**The OBX format (invented here, documented in OnyxBoxModule.h):**

```
offset 0: magic "OBX1" (4 bytes)
offset 4: u32 count (little-endian)
then count entries, packed:
    u16 nameLen | name bytes (UTF-8) | u8 kind (0=blob,1=image,2=text)
    u32 payloadOffset (absolute) | u32 payloadSize
image payload: u16 width | u16 height | width*height*4 bytes RGBA8
text payload:  raw UTF-8
```

**Module contract:** id `"obx"`, displayName "OnyxBox (example)", hints
{"obx"}, filter {"OnyxBox", {"obx"}}. Probe: header magic OBX1 → {95,
"OBX1 magic at 0"}; extension .obx without magic → {20, "extension only"};
else {0, "no evidence"}. Types: `obx.image`, `obx.text`, `obx.blob`
(labels Image/Text/Blob, MediaKind Image/Text/Unknown). ParseContainer:
walks the TOC; an entry whose payload range exceeds the file is emitted
with `flags=Failed` + diag `{Error, "obx.entry.range",
"payload out of bounds"}` and the walk continues (salvage). Decoders:
Image for `obx.image` (payload → TextureData, name/width/height/pixels),
Text for `obx.text`. ModuleState stores the parsed TOC so decoders can
find payload ranges without re-walking.

- [ ] **Step 1: Failing E2E tests** — a test helper writes a well-formed
  .obx to a temp file (2 entries: an 8x8 image with a deterministic
  gradient `pixel[i] = uint8_t(i * 7)`, a text "hello box") plus one
  corrupt entry (payloadOffset beyond EOF). Then, through Workspace +
  DecoderRegistry ONLY (no legacy types):

```cpp
TEST_CASE("onyxbox end to end: probe, open, tree, decode, salvage") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(std::make_unique<OnyxBox::OnyxBoxModule>());
    auto obx = WriteSampleBox();  // helper above
    auto rank = ws.Probe(obx);
    REQUIRE(rank.winner != nullptr);
    CHECK(rank.rows[0].result.confidence == 95);

    auto id = ws.Open(obx);
    auto* doc = ws.Get(id);
    REQUIRE(doc); REQUIRE(doc->roots.size() == 3);
    CHECK(doc->roots[2].flags == Onyx::Domain::NodeFlags::Failed);
    CHECK(doc->diags.Count(Onyx::Services::Severity::Error) == 1);

    // decode image
    auto& reg = ws.Decoders();
    REQUIRE(reg.HasImage(doc->roots[0].typeId));
    Onyx::Services::Progress prog;
    Onyx::Modules::DecodeContext ctx{*doc, doc->roots[0], doc->diags, prog};
    auto img = reg.DecodeImage(ctx);
    REQUIRE(img); CHECK(img->width == 8); CHECK(img->height == 8);
    CHECK(img->pixels[9] == uint8_t(9 * 7));

    // decode text
    Onyx::Modules::DecodeContext ctx2{*doc, doc->roots[1], doc->diags, prog};
    auto txt = reg.DecodeText(ctx2);
    REQUIRE(txt); CHECK(txt->text == "hello box");
    std::filesystem::remove(obx);
}
```

  Plus: `TEST_CASE("onyxbox types are minted in the obx namespace")` —
  after AddModule, `catalog.Find("obx.image").valid()`.
- [ ] **Step 2: Run, expect failure**
- [ ] **Step 3: Implement the module**
- [ ] **Step 4: Build + full ctest green**
- [ ] **Step 5: Commit** — `feat(examples): OnyxBox - a synthetic module proving the v1 contracts`

### Task 5: Material roles replace positional layers

**Files:**
- Modify: `Include/Onyx/Parsers/SceneNode.h` (MaterialDesc replaces
  MaterialInfo; flat texture list; pbrLayers deleted)
- Modify: `Include/Onyx/Rendering/SceneRenderer.h` (RenderBatch role slots)
- Modify: `Source/Rendering/SceneRenderer.cpp` (Build + bind by role)
- Test: compile-level + existing suite (no GL in tests); plus a pure unit
  test on the one extracted helper (below) in `Tests/materialdesc_test.cpp`
  (ctest OnyxMaterialDesc)

**Interfaces (verbatim):**

```cpp
// SceneNode.h — replaces MaterialInfo and the nested textures vector:
enum class TextureRole : uint8_t {
    Diffuse, Normal, Occlusion, Gloss, Height, Scatter, Detail, Emissive, EnvMap
};

struct MaterialDesc {
    float       baseColor[4]  = {1, 1, 1, 1};
    float       blendColor[4] = {1, 1, 1, 1};
    BlendMode   blendMode     = BlendMode::Normal;
    float       uvOffset[2]   = {0, 0};
    // role -> index into SceneData::textures; absent role = no map.
    std::map<TextureRole, int> textures;
};

// SceneData changes:
std::vector<MaterialDesc>                     materials;  // index = materialId
std::vector<std::unique_ptr<TextureData>>     textures;   // FLAT pool
// DELETED: pbrLayers, the old nested textures, MaterialInfo.
```

- A pure helper in SceneRenderer.cpp, declared in SceneRenderer.h so it is
  testable without GL:
  `static std::array<int, 9> ResolveRoleIndices(const MaterialDesc&);`
  returning the texture-pool index per role (enum order), -1 when absent.
- `Build()` uploads the flat pool once (index-aligned GLuint vector),
  fills the batch's role slots via ResolveRoleIndices: texture0=Diffuse,
  texture1=EnvMap, texNormal=Normal, texAO=Occlusion, texGloss=Gloss,
  texScatter=Scatter (Height/Detail/Emissive resolved but unbound —
  comment says the shader work for them is M4).
- `part.textureLayer` no longer indexes materials; it survives only as the
  additive-ordering hint (the `blendMode != Normal || textureLayer > 0`
  branch keeps reading it). GOW2's per-part variant selection becomes a
  load-time concern (its future module emits one MaterialDesc per variant)
  — record this in the SceneNode.h comment.
- Viewport3D/MinimalViewer: verify they compile; neither constructs
  MaterialInfo directly today (they consume SceneData whole) — if that
  turns out false, fix the construction sites in the same commit.

- [ ] **Step 1: Failing test** — `ResolveRoleIndices` on: an empty
  MaterialDesc (all -1); a full 9-role map (each slot echoes its index);
  a sparse {Diffuse:3, Gloss:0} map.
- [ ] **Step 2: Run, expect failure**
- [ ] **Step 3: Implement the type swap and renderer surgery**
- [ ] **Step 4: Build + full ctest green** (LayerGuard included)
- [ ] **Step 5: Commit** — `feat(render)!: Materials carry explicit texture roles

BREAKING: MaterialInfo/pbrLayers removed; SceneData textures are a flat
pool indexed by MaterialDesc roles. Consumers pinned to v0.6.x are
unaffected until they port.`

### Task 6: The generic headless CLI

**Files:**
- Create: `Include/Onyx/Cli/Commands.h`
- Create: `Source/Cli/Commands.cpp`
- Create: `Examples/OnyxCli/Main.cpp` + `Examples/OnyxCli/CMakeLists.txt`
  (executable `onyxbox-cli`, links Onyx_Core + Onyx_ExampleBox; added via
  add_subdirectory, guarded by ONYX_BUILD_EXAMPLES)
- Modify: root `CMakeLists.txt` (ONYX_CORE_SOURCES += Commands.cpp;
  add_subdirectory)
- Test: `Tests/cli_test.cpp` (ctest OnyxCli) — calls the command functions
  directly with an ostringstream; no process spawning.

**Interfaces:**

```cpp
// Include/Onyx/Cli/Commands.h
#pragma once
#include <Onyx/Modules/Workspace.h>
#include <iosfwd>

namespace Onyx::Cli {

// Exit codes, shared by every command:
//   0 ok · 1 bad usage/file/entry not found · 2 no module accepted
//   3 --strict and the document has Error diags
inline constexpr int kOk = 0, kUsage = 1, kNoModule = 2, kStrictErrors = 3;

int CmdProbe (Modules::Workspace&, const std::filesystem::path&,
              std::ostream& out);                    // score/reason table
int CmdList  (Modules::Workspace&, const std::filesystem::path&,
              bool json, std::ostream& out);         // tree; json = one object
int CmdExtract(Modules::Workspace&, const std::filesystem::path&,
              const std::filesystem::path& outDir, std::ostream& out);
int CmdDecode(Modules::Workspace&, const std::filesystem::path&,
              std::string_view entryName, bool strict, std::ostream& out);
              // decodes by capability (image->PNG-less summary: type,
              // dimensions, byte count; text->prints it); emits diags

// argv dispatcher used by example/consumer mains:
int Run(Modules::Workspace&, int argc, char** argv,
        std::ostream& out, std::ostream& err);
} // namespace Onyx::Cli
```

JSON for `CmdList --json` is hand-emitted like the M2 report style (no
nlohmann in the SDK): `{"path":..., "module":..., "entries":[{"name":...,
"type":"obx.image","size":N,"failed":false,"children":[...]}], "diags":[...]}`.
Diag output rule: every command drains the document's diags to `out` as
`[severity] code: message` lines; `CmdDecode(strict=true)` returns
kStrictErrors when any Error was reported.

- [ ] **Step 1: Failing tests** — all through onyxbox fixtures:
  probe table contains both rows and the winner line; list --json contains
  `"type":"obx.image"` and `"failed":true` for the corrupt entry; extract
  writes payloadSize bytes per good entry and skips Failed ones (assert
  file sizes); decode "hello box" prints the text and returns kOk; decode
  with strict=true on the corrupt-carrying box returns kStrictErrors;
  unknown entry name → kUsage; a file no module takes → kNoModule;
  `Run` dispatches "probe"/"list"/"extract"/"decode" and prints usage to
  err with kUsage for junk.
- [ ] **Step 2: Run, expect failure**
- [ ] **Step 3: Implement** (Commands.cpp; Main.cpp is 20 lines:
  Workspace + AddModule(OnyxBoxModule) + Cli::Run).
- [ ] **Step 4: Build + full ctest green; also run the example binary once
  by hand against a generated .obx and paste its output in the report**
- [ ] **Step 5: Commit** — `feat(cli): Generic probe/list/extract/decode commands`

### Task 7: Milestone gate

- [ ] **Step 1:** Clean-configure scratch build (`build-gate`, delete
  after): full build, full ctest, `ctest -R "OnyxWorkspace|OnyxBox|OnyxCli"
  --repeat until-fail:5`.
- [ ] **Step 2:** LayerGuard green; `layerguard-core.txt` lists the new
  Modules/ and Cli/ sources.
- [ ] **Step 3:** CHANGELOG under Unreleased (module contracts, workspace,
  decoders, onyxbox, material roles BREAKING note, CLI); roadmap: mark M3a
  done, M3b next. Commit `docs: Record the M3a module contracts`.

---

## Self-review notes

- Spec coverage: §5 contracts ✔T1; §5.1 ranked probe with floor+tie ✔T1;
  §5.3 ContainerContext without PrepareForParse ✔T2; §5.4 tree+Failed
  flags on the existing AssetEntry (spec's `AssetNode` name deferred — the
  struct already matches; renaming is churn without benefit, recorded
  deviation) ✔T2; §6 decoders keyed by output ✔T3 (Audio/Schema slots
  deferred YAGNI, recorded); §7.1 salvage exercised by onyxbox ✔T4; §8.1
  roles ✔T5; §11 CLI probe/list/extract/decode ✔T6 (render arrives with
  the M4 oracle; recorded in roadmap). Workspace-owned EventBus/JobQueue
  wiring ✔T2. Mounts declared in T1, consumed by no one until a
  disc-image module exists — contract only, recorded.
- Type consistency: DocumentId/ContainerContext/ModuleState defined once
  in Workspace.h, consumed by Tasks 3/4/6 under those exact names;
  NodeFlags::Failed used identically in T2/T4/T6.
- Placeholders: T2's elided test cases are named with their exact asserted
  behavior and the implementer is told to write them in full — acceptable
  because the behaviors are one-line contracts stated in the same task.
