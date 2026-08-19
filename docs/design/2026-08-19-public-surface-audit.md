# The public-surface audit — can a third party build a toolkit on Onyx?

**Milestone:** Onyx v1 · M5 generality · Task 2
**Branch:** `feat/onyx-v1-m5-generality` (audit performed at `da7da07`)
**Date:** 2026-08-19
**Verdict:** **6 gaps, 1 of them blocking.** The v1.0 tag is not yet justified
by this audit alone — `Onyx::Cli::CmdRender` (G1) must be fixed first.
Everything else is a workaround or cosmetic.

This task replaces the scrapped "build a second toolkit inside `Examples/`"
exam (scope cut by Jean, 2026-08-19: no game module enters this repo). It
must extract the same information the built exam would have: **what can a
third-party consumer NOT reach through `Include/Onyx/**`?**

The subjects are the three things already in `Examples/` — `OnyxBox` (the
module), `MinimalViewer` (the GUI toolkit), `OnyxCli` (the headless
toolkit) — audited *as if they were third-party*, on the working
assumption that they may be passing by accident: they were written by the
same people who wrote the SDK, in-tree, with full access.

---

## SDK gaps — ranked

| # | What the consumer wanted | Where it should live | Severity |
|---|---|---|---|
| **G1** | To call `Onyx::Cli::CmdRender` — the SDK's own headless "decode a Scene entry → PNG" command — from their own CLI. The header is public and compiles; **the symbol ships in no `Onyx::*` library.** Its only definition is `Examples/OnyxCli/Render.cpp`, compiled straight into the example executable. A consumer's only route is to copy that source file out of `Examples/`. | A new shipped target, e.g. `Onyx::CliRender` (links `Onyx_Core` + `Onyx_Render`), owning `Render.cpp`. It cannot go in `Onyx_Core` — `Onyx_Render` already links `Onyx_Core` PUBLIC, so that direction is a real link cycle; the current placement is a *correct* diagnosis with an *incomplete* fix. Declaration stays in `Include/Onyx/Cli/Render.h`. | **blocks a toolkit** |
| **G2** | `#include <Onyx/Onyx.h>` to actually mean "the full public surface", as its own header comment claims. It reaches **49 of 108** public headers. The 59 it misses include the *entire* renderer, *every* concrete viewer, the decoder-registration entry point, selection, viewer routing/opening, the whole CLI, the whole TestKit, theming, and `PathUtils`. Proven live below (cold-start variants B and C). | `Include/Onyx/Onyx.h`, plus (recommended) a sibling `Include/Onyx/Render.h` so `volk.h` is not dragged into every consumer TU by the umbrella. Itemised in the Capability audit. | **forces a workaround** |
| **G3** | `-I <sdk>/Include` alone to satisfy `<Onyx/Onyx.h>`. It does not: the umbrella's **first** include is `<Onyx/Version.h>`, and that file **does not exist in the source tree** — it is generated into `build/generated/Onyx/Version.h`. Consumers must also put a *build* directory on their include path. | `Include/Onyx/Version.h` should be the generated file's install destination (and be `install()`ed — see G4), or the umbrella should not hard-depend on a generated header. | **forces a workaround** |
| **G4** | To consume Onyx as a package. There is **no `install()`, no `export()`, no `find_package` config anywhere in the tree** (`grep -n 'install(\|export(\|CMakePackageConfig\|write_basic_package' CMakeLists.txt` → zero hits). The only supported consumption is `add_subdirectory()` over the full source tree. `Include/Onyx/**` is a directory that is never installed anywhere. | Root `CMakeLists.txt`: `install(DIRECTORY Include/Onyx …)`, `install(TARGETS … EXPORT OnyxTargets)`, `OnyxConfig.cmake`. | **forces a workaround** (vendoring works, and the examples prove it — but "third-party consumer" is doing real work in that sentence) |
| **G5** | Public headers not to define names at global scope. `Include/Onyx/Services/PathUtils.h` declares `namespace PathUtils` at **global** scope (not `Onyx::Services`) and unconditionally `#define WIN32_LEAN_AND_MEAN` + `#include <windows.h>` on Win32. `Include/Onyx/Domain/Entry.h` — which **is** in the umbrella closure — ends with `using AssetEntry = Onyx::Domain::AssetEntry;` at global scope, so `::AssetEntry` lands in every consumer that includes `<Onyx/Onyx.h>`. | `Include/Onyx/Services/PathUtils.h` (move into `Onyx::Services`), `Include/Onyx/Domain/Entry.h` (drop the back-compat alias). Both are source-compatibility breaks for in-tree callers. | cosmetic |
| **G6** | Example/consumer CMake to use only the namespaced aliases. `Examples/OnyxCli` links `Onyx_Core` and `Onyx_ExampleBox`; `Examples/OnyxBox` links `Onyx_Core` PUBLIC — raw target names, not `Onyx::Core`. Identical behaviour in-tree; breaks the moment G4's export ships only `Onyx::` names (and loses CMake's "typo becomes a hard error at configure time" guarantee that `::` gives you). | `Examples/OnyxCli/CMakeLists.txt`, `Examples/OnyxBox/CMakeLists.txt`. | cosmetic |

**What is NOT a gap** (each was actively hunted for and came back clean):

- No `#include` anywhere in `Examples/**` reaches `Source/**` or `Tools/**`. Zero.
- No example target puts `Source/` on its include path — verified against the
  *generated* `build/build.ninja`, not just the CMake source.
- No example compiles an SDK source directly out of `Source/`.
- **No stubs.** `AppConfigStub.cpp` is genuinely gone (only historical
  comments survive), and no equivalent exists anywhere.
- All **108** public headers compile standalone against `Include/` +
  third-party. 108/108.
- Every public symbol the examples use links out of a shipped `Onyx_*.lib`
  — **except** `CmdRender` (G1).
- The minimal cold-start toolkit **compiles** with no `-I Source`, no `-I .`.

---

## 1. Include audit

Every `#include` in `Examples/**`, classified. **Private** = `Source/**`,
`Tools/**`, or a relative include escaping its own example directory.

| File | Public `<Onyx/…>` | Third-party | Own-dir relative | Cross-example | **Private** | std |
|---|---|---|---|---|---|---|
| `Examples/MinimalViewer/Source/Main.cpp` | 13 | — | 2 (`SelfTest.h`, `HexViewer.h`) | 1 (`<OnyxBoxModule.h>`) | **0** | 6 + `<windows.h>` |
| `Examples/MinimalViewer/Source/HexViewer.h` | 1 | — | — | — | **0** | 3 |
| `Examples/MinimalViewer/Source/HexViewer.cpp` | — | 1 (`<imgui.h>`) | 1 | — | **0** | 1 |
| `Examples/MinimalViewer/Source/SelfTest.h` | — | — | — | — | **0** | 0 |
| `Examples/MinimalViewer/Source/SelfTest.cpp` | 3 | — | 2 | — | **0** | 3 |
| `Examples/OnyxBox/OnyxBoxModule.h` | 5 | — | — | — | **0** | 5 |
| `Examples/OnyxBox/OnyxBoxModule.cpp` | 4 | — | 1 | — | **0** | 6 |
| `Examples/OnyxCli/Main.cpp` | 4 | — | — | 1 (`<OnyxBoxModule.h>`) | **0** | 7 |
| `Examples/OnyxCli/Render.cpp` | 11 | 3 (`<glm/glm.hpp>`, `<glm/gtc/matrix_transform.hpp>`, `<stb_image_write.h>`) | — | — | **0** | 9 |
| **Total** | **41** | **4** | **6** | **2** | **0** | **40** |

**Findings from this table:** none of severity. Detail on the two
non-obvious columns:

- **Cross-example (2 hits).** `<OnyxBoxModule.h>` in `MinimalViewer/Main.cpp`
  and `OnyxCli/Main.cpp` resolves through
  `target_include_directories(Onyx_ExampleBox PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})`
  — it escapes its own example directory, which the brief's rule flags. It is
  **not** an SDK gap: `OnyxBox` is *the consumer's own game module*, the exact
  role the cut toy-game module would have played. A third-party toolkit
  consumes its own module the same way. Recorded, not counted against the SDK.
- **`<windows.h>`** in `Main.cpp` is behind `#ifdef _WIN32` for `WinMain` —
  platform, not private.

The whole point of the private column: **it is zero**. The examples reach the
SDK exclusively through `Include/Onyx/**`. Cross-checked against the
*generated* build, not just the CMake text — `build/build.ninja`'s `INCLUDES`
line for `MinimalViewer`'s objects is:

```
-I…\Examples\MinimalViewer\Source -I…\Include -I…\build\generated
-I…\_deps\glm-src -I…\_deps\lz4-src\lib -I…\build\generated\shaders
-I…\_deps\volk-src -I…\_deps\vulkan_headers-src\include -I…\_deps\vma-src\include
-I…\_deps\imgui-src -I…\_deps\imgui-src\backends -I…\_deps\glfw-src\include
-I…\_deps\implot-src -I…\_deps\imgui_color_text_edit-src
-I…\third_party\miniaudio -I…\_deps\ffmpeg_prebuilt-src\include
-I…\Examples\OnyxBox
```

No `Source/`. The mechanism is `onyx_apply_common()` in the root
`CMakeLists.txt`, which lists `${CMAKE_CURRENT_SOURCE_DIR}/Source` as
**PRIVATE** — so a private header is not merely unused by the examples, it is
*unreachable* from them. That is the structural guarantee, and it holds.

### Header self-sufficiency sweep (stronger than the include audit)

The include audit only proves the examples *happen* not to need private
headers. The stronger question is whether a public header secretly needs one.
One TU per public header (`#include <that/header.h>` + `int main(){}`), all
108, compiled with `-I Include -I build/generated` + third-party only:

```
cpp: 108   obj: 108
grep -l 'error C' *.log  →  (nothing)
```

**108/108 clean.** No public header requires anything from `Source/`.

---

## 2. Link audit

| Target | `Onyx::*` aliases | Raw SDK target names | Third-party | SDK sources compiled directly | Stubs | Other targets' objects |
|---|---|---|---|---|---|---|
| `MinimalViewer` | `Onyx::Onyx`, `Onyx::Render` | — | (transitive) | none | **none** | none |
| `Onyx_ExampleBox` | — | `Onyx_Core` (PUBLIC) — **G6** | — | none | **none** | none |
| `onyxbox-cli` | `Onyx::Render` | `Onyx_Core`, `Onyx_ExampleBox` — **G6** | `-I third_party/stb` (header-only, vendored) | **`Examples/OnyxCli/Render.cpp`** — see below | **none** | none |

**`Render.cpp` — the one real link finding (G1).** `onyxbox-cli` adds
`${CMAKE_CURRENT_SOURCE_DIR}/Render.cpp` to its own source list. Read
narrowly this is not "compiling an SDK source directly" — the file lives in
`Examples/`, not `Source/`. Read for *intent* it is exactly that: the file
implements `Onyx::Cli::CmdRender`, a function **declared in a public SDK
header** (`Include/Onyx/Cli/Render.h`), and it is the SDK's own code sitting
in the example directory because there was nowhere else to put it.

Evidence that this is not a stylistic quibble — searching every built archive
for the symbol:

```
CmdDecode found in build/Onyx_Core.lib
CmdRender  →  (no library)
```

`CmdDecode`, declared next door in `Include/Onyx/Cli/Commands.h`, ships.
`CmdRender` does not. Every other public symbol the examples use resolves out
of a shipped library:

| Symbol | Archive |
|---|---|
| `ImageViewer` | `Onyx_Shell.lib` |
| `Viewport3D` | `Onyx_Shell.lib`, `Onyx_Media.lib` |
| `TextEditorViewer` | `Onyx_Shell.lib` |
| `OpenSelection` | `Onyx_Shell.lib` |
| `RouteForType` | `Onyx_Shell.lib` |
| `ApplyTheme` (`Onyx::Theme`) | `Onyx_Shell.lib` |
| `CmdProbe/List/Extract/Decode`, `Run` | `Onyx_Core.lib` |
| **`CmdRender`** | **none** |

The reasoning in `Include/Onyx/Cli/Render.h`'s header comment is *correct*
about the cycle (`Onyx_Render` links `Onyx_Core` PUBLIC, so `Onyx_Core`
cannot link `Onyx_Render`) and *correct* about the layer-completeness glob
over `Source/*.cpp` forcing the file out of `Source/`. The conclusion it
draws — "so it lives in the executable" — is where it stops one step short:
the cycle-free home is a **fourth library above both**, not an executable.

**`AppConfigStub` — verified gone.** M4's history had per-executable
`AppConfigStub.cpp` files for `onyxbox-cli` and `Tools/OnyxOracle`, needed
because `Onyx::Rendering::Camera` called `AppConfig::Get()`. Searching the
whole tree for `stub` (case-insensitive, excluding `build/` and
`third_party/`) returns **eight hits, all of them prose**: comments in
`Examples/OnyxCli/CMakeLists.txt`, `Tools/OnyxOracle/CMakeLists.txt`,
`Include/Onyx/RenderVk/SceneRendererVk.h`, `Source/Rendering/Camera.cpp`
recording that the stub is no longer needed, plus three unrelated uses of the
word ("empty stubs" for Export glTF, a test double in
`Tests/documentwindow_test.cpp`). **No stub file exists.** No equivalent
workaround (no `weak` symbol, no `-force:multiple`, no duplicated TU) exists
either. Confirmed clean.

---

## 3. Capability audit

For each thing the examples actually do: is the enabling API declared in a
public header, and is it reachable by transitively following
`#include <Onyx/Onyx.h>`?

Reachability is computed as the transitive closure of `Onyx/`-prefixed
includes from `Include/Onyx/Onyx.h` over `Include/` + `build/generated/`:
**49 of the 108 headers under `Include/` reachable, 59 unreachable.** (The 50th
member of the closure is the generated `Onyx/Version.h`, which is not in
`Include/` at all — see G3.)

| Capability | Exercised by | Enabling API | Public header? | Reachable from `Onyx.h`? |
|---|---|---|---|---|
| Register a game module | `MinimalViewer`, `onyxbox-cli` | `Modules::IGameModule`, `App::AddModule`, `Workspace::AddModule` | ✅ `Modules/GameModule.h`, `App/App.h`, `Modules/Workspace.h` | ✅ |
| Register asset types | `OnyxBox` | `Types::TypeRegistrar::Add`, `Types::TypeInfo` | ✅ `Types/TypeRegistrar.h`, `Types/TypeCatalog.h` | ✅ |
| **Register decoders** | `OnyxBox` | `Modules::DecoderRegistry::{Scene,Image,Text}`, `DecodeContext` | ✅ `Modules/DecoderRegistry.h` | ❌ **only forward-declared** via `GameModule.h`; the type is incomplete |
| Probe / claim a file | `OnyxBox` | `Modules::ProbeInput/ProbeResult` | ✅ `Modules/GameModule.h` | ✅ |
| Mount a container VFS | `OnyxBox` | `Modules::MountSpec`, `Vfs::IVirtualFileSystem` | ✅ `Modules/GameModule.h`, `Vfs/IVirtualFileSystem.h` | ✅ |
| Open documents (async) | `MinimalViewer` | `Workspace::OpenAsync`, `Modules::TreeReady` | ✅ `Modules/Workspace.h` | ✅ |
| Subscribe to events | `MinimalViewer` | `Services::EventBus`, `Services::Subscription` | ✅ `Services/EventBus.h` | ✅ |
| Browse the tree (UI) | `MinimalViewer` | `App::Panels::DocumentBrowser` (auto-registered by `App::registerPanels`) | ✅ `App/Panels/DocumentBrowser.h` | ❌ (works anyway — the panel is registered by the Shell, the consumer never names it) |
| **Select a node** | `MinimalViewer` | `Modules::NodePath`, `SelectionChanged`, `Resolve` | ✅ `Modules/Selection.h` | ❌ |
| **Route a type to a viewer** | `MinimalViewer` | `App::RouteForType`, `App::ViewerKind` | ✅ `App/ViewerRouting.h` | ❌ |
| **Open a decoded entry** | `MinimalViewer` | `App::ViewerOpener`, `App::OpenSelection` | ✅ `App/ViewerOpening.h` | ❌ |
| Add a panel | `MinimalViewer` | `App::IPanel`, `App::addPanel`, `App::setPanelVisible`, `App::PanelRegistry` | ✅ `App/IPanel.h`, `App/App.h`, `App/PanelRegistry.h` | ✅ |
| Add a viewer (custom tab) | `MinimalViewer` (`HexViewer`) | `Viewers::IDocumentContent`, `DocumentWindow::AddTab`, `App::ViewerRegistry` | ✅ `Viewers/IDocumentContent.h`, `Viewers/DocumentWindow.h`, `App/ViewerRegistry.h` | ✅ |
| **View an image** | `MinimalViewer --open-first-image` | `Viewers::ImageViewer` | ✅ `Viewers/ImageViewer.h` | ❌ |
| **View a 3D scene** | `MinimalViewer --open-first-scene` | `Viewers::Viewport3D` | ✅ `Viewers/Viewport3D.h` | ❌ |
| **View text** | (SDK capability, not exercised) | `Viewers::TextEditorViewer` | ✅ `Viewers/TextEditorViewer.h` | ❌ |
| **View video** | (SDK capability, not exercised) | `Viewers::VideoPlayer` | ✅ `Viewers/VideoPlayer.h` | ❌ |
| Run the frame loop / window | `MinimalViewer` | `App::Window`, `Window::initNative`, `Window::run` | ✅ `App/Window.h` | ✅ |
| Mark the main thread | `MinimalViewer` | `Threading::MarkMainThread` | ✅ `Services/Threading.h` | ✅ |
| Log | `MinimalViewer` | `LOG_INFO/WARN/ERR` | ✅ `Services/Logger.h` | ✅ |
| Persist config / layout | `MinimalViewer` (implicit) | `Services::AppConfig`, `App::SetDefaultLayout` | ✅ `Services/AppConfig.h`, `App/App.h` | ✅ |
| **Theme** | UI Gallery panel | `Onyx::Theme::ApplyTheme`, `BuildPalette`, `SetColorOverride` | ✅ `Services/ThemeManager.h` | ❌ |
| **Resolve resource paths** | (SDK-internal; a consumer needs it for fonts) | `PathUtils::resolvePath` | ✅ `Services/PathUtils.h` | ❌ (and global-namespace — G5) |
| **Run the generic CLI** | `onyxbox-cli` | `Cli::Run`, `CmdProbe/List/Extract/Decode`, `kOk/kUsage/…` | ✅ `Cli/Commands.h` | ❌ |
| **Render headless → PNG** | `onyxbox-cli render` | `Cli::CmdRender` | ✅ `Cli/Render.h` | ❌ **and unlinkable — G1** |
| **Drive the renderer directly** | `Examples/OnyxCli/Render.cpp` | `Rendering::VkContext`, `SceneRendererVk`, `OffscreenTarget`, `ScenePipelines`, `VulkanProjection`, `Resources::OneShot`, `ShadingMode`, `Camera` | ✅ `RenderVk/*.h` (7), `Rendering/*.h` (5) | ❌ **none of the 12 — the umbrella exposes no renderer at all** (M4's finding, confirmed) |
| Read files | all | `Vfs::OsFile`, `IFile`, `MemoryFile`, `IsoFileSystem` | ✅ `Vfs/*.h` | ✅ |
| Be "born testable" | (SDK capability, M5 T1) | `TestKit::Goldens/DecodeSmoke/RenderCompare` | ✅ `TestKit/*.h` | ❌ (also not in the `Onyx::Onyx` aggregate — `Onyx::TestKit` is a deliberate opt-in; the header omission is not) |

### The 59 headers `Onyx.h` does not reach

Grouped, so G2 can be scheduled as a single edit with an informed decision
per group:

| Group | Headers | Should the umbrella pull it in? |
|---|---|---|
| Renderer — Vulkan | `RenderVk/{OffscreenTarget,Pipelines,RenderContext,SceneRendererVk,TexturePool,VkContext,VkResources}.h` | **No — via a sibling `<Onyx/Render.h>`.** Pulling these into the umbrella drags `volk.h`/`vk_mem_alloc.h` into every consumer TU, including headless ones. |
| Renderer — shared | `Rendering/{AnimationPlayer,AxisGizmo,Camera,JointPalette,RenderBatch}.h` | Same sibling header. |
| Viewers | `Viewers/{ImageViewer,TextEditorViewer,VideoPlayer,Viewport3D}.h` | **Yes** — `DocumentWindow` is already in; the things you put in it are not. |
| Module contract | `Modules/{DecoderRegistry,Selection}.h` | **Yes** — `DecoderRegistry` is half the `IGameModule` contract and is currently only forward-declared. |
| Viewer plumbing | `App/{ViewerOpening,ViewerRouting}.h` | **Yes** |
| CLI | `Cli/{Commands,Render}.h` | **Yes** (and see G1) |
| TestKit | `TestKit/{DecodeSmoke,Goldens,RenderCompare}.h` | Sibling `<Onyx/TestKit.h>` — matches the opt-in `Onyx::TestKit` target. |
| Services | `Services/{Appearance,AssetVisibility,EventManager,Events,FrameScheduler,Metrics,PathUtils,TaskManager,ThemeManager}.h` | **Yes** for `ThemeManager`, `PathUtils`, `Events`, `Metrics`; judgement call on the rest. |
| Container / schema | `Container/{ChunkReader,ChunkSchema,ChunkTree}.h`, `Schema/{AssetFormat,NodeInstance}.h` | **Yes** — a module author parsing a chunked container wants these. |
| UI helpers | `App/{Formatting,InfoTab,InfoTabFilter,StatusBarFormat,TexturePool,TypeVisuals,UIHelpers,Widgets}.h`, `App/Panels/*` (7), `Fonts/IconTable.h` | Judgement call — mostly Shell-internal. |
| Misc | `Audio/AdpcmDecoder.h`, `Platform/SystemTheme.h`, `Vfs/{SliceFile,TransformFile}.h` | **Yes** for the two `Vfs` ones (they are file adapters a module author composes). |

---

## 4. The cold-start test

The real experiment: write the smallest plausible third-party toolkit
`main()` — register a module, run the app — and compile it as a throwaway
TU against **only** the public surface. No `-I Source`. No `-I .`. No
`-I Examples`.

```
cl /c /EHsc /std:c++20 /utf-8
   /DNOMINMAX /DONYX_OS_WINDOWS /DVK_NO_PROTOTYPES /DGLFW_EXPOSE_NATIVE_WIN32
   /DGLM_FORCE_DEPTH_ZERO_TO_ONE /DIMGUI_IMPL_VULKAN_USE_VOLK /DVMA_STATIC_VULKAN_FUNCTIONS=0
   -I <sdk>/Include
   -I <sdk>/build/generated                       ← G3: Version.h lives ONLY here
   -I <sdk>/build/_deps/glm-src
   -I <sdk>/build/_deps/imgui-src
   -I <sdk>/build/_deps/imgui-src/backends
   -I <sdk>/build/_deps/glfw-src/include
   -I <sdk>/build/_deps/volk-src
   -I <sdk>/build/_deps/vulkan_headers-src/include
   ColdStart.cpp
```

The `/D` flags are not a cheat: they are the `INTERFACE` compile definitions
`Onyx::Onyx`/`Onyx::Render` already propagate to anything that links them
(`onyx_apply_common()` + `target_compile_definitions(… PUBLIC)`), so a
consumer using CMake gets them for free.

### Variant A — the cold-start toolkit → **compiles, exit 0**

```cpp
// ColdStart.cpp — the smallest plausible third-party toolkit.
// Registers one game module and runs the app. Public headers ONLY.
#include <Onyx/Onyx.h>

#include <memory>
#include <vector>

namespace MyToolkit {

class MyModule final : public Onyx::Modules::IGameModule {
public:
    Onyx::Modules::ModuleInfo Info() const override {
        Onyx::Modules::ModuleInfo info;
        info.id          = "mine";
        info.displayName = "My Game";
        info.hints       = {"mine"};
        info.openFilters = {{"My Game archives", {"pak"}}};
        return info;
    }

    Onyx::Modules::ProbeResult Probe(const Onyx::Modules::ProbeInput& in) const override {
        if (!in.header.empty() && static_cast<unsigned char>(in.header[0]) == 'M')
            return {90, "magic 'M' at 0"};
        return {0, "no magic"};
    }

    void RegisterTypes(Onyx::Types::TypeRegistrar& reg) override {
        Onyx::Types::TypeInfo spec;
        spec.key   = "blob";
        spec.label = "Blob";
        spec.media = Onyx::Domain::MediaKind::Raw;
        m_blob = reg.Add(spec);
    }

    void RegisterDecoders(Onyx::Modules::DecoderRegistry& reg) override;   // ← see Variant B

    Onyx::Modules::ParseResult ParseContainer(Onyx::Modules::ContainerContext& ctx) override {
        Onyx::Domain::AssetEntry root;
        root.name   = "root";
        root.typeId = m_blob;
        ctx.roots.push_back(std::move(root));
        return {true};
    }

private:
    Onyx::Types::TypeId m_blob{};
};

} // namespace MyToolkit

int main() {
    Onyx::Threading::MarkMainThread();
    Onyx::App::Window::initNative();
    Onyx::App::Window window;
    window.app().SetRegistrar([](Onyx::App::App& app) {
        app.AddModule(std::make_unique<MyToolkit::MyModule>());
    });
    window.run();
    return 0;
}
```

**Result: `EXITCODE=0`.** The public surface stands alone for the boot path.
Note the one thing this variant quietly ducks: `RegisterDecoders` is
*declared* without a body, which is legal precisely because
`DecoderRegistry` may stay incomplete. That is the seam Variant B pulls on —
and it is exactly the "passing by accident" failure mode this audit exists
to catch. A cold-start test that stopped here would have reported "all clean"
and been wrong.

### Variant B — the same toolkit doing real work, umbrella only → **fails**

Same file, but `RegisterDecoders` gets a body, a selection is routed to a
viewer, a viewport is constructed, the app is themed, a resource path is
resolved. Still `#include <Onyx/Onyx.h>` and nothing else.

```
error C2027: use of undefined type 'Onyx::Modules::DecoderRegistry'
   note: see declaration of 'Onyx::Modules::DecoderRegistry'  (GameModule.h:46)
error C2039: 'NodePath': is not a member of 'Onyx::Modules'
error C3083: 'ThemeManager': the symbol to the left of '::' must be a type
error C2039: 'Get': is not a member of 'Onyx::Services'
error C3083: 'PathUtils': the symbol to the left of '::' must be a type
error C2039: 'resolvePath': is not a member of 'Onyx::Services'
EXITCODE=2
```

(`ViewerOpener` / `OpenSelection` / `ImageViewer` / `Viewport3D` failures are
present but masked by cascading syntax errors from the `NodePath` failure.)

Every one of these is **G2**, not a missing API.

### Variant C — Variant B plus the eight missing public includes → **compiles, exit 0**

```cpp
#include <Onyx/Onyx.h>

#include <Onyx/Modules/DecoderRegistry.h>   // ← none of these eight are
#include <Onyx/Modules/Selection.h>         //   reachable from the umbrella
#include <Onyx/App/ViewerOpening.h>
#include <Onyx/App/ViewerRouting.h>
#include <Onyx/Viewers/ImageViewer.h>
#include <Onyx/Viewers/Viewport3D.h>
#include <Onyx/Services/ThemeManager.h>
#include <Onyx/Services/PathUtils.h>
```

```
EXITCODE=0   (one warning: APIENTRY redefined — GLFW vs. minwindef, benign)
```

**This is the decisive result.** Every API Variant B wanted exists, is
public, and is self-sufficient. The *only* thing standing between a
third-party consumer and a working toolkit is knowing eight header paths the
documented umbrella does not surface. That is G2's severity justification:
**forces a workaround, does not block.**

### What the cold-start test cannot prove

It is a `cl /c` compile — it proves the **headers** stand alone. It says
nothing about linking, which is where G1 lives: a Variant D calling
`Onyx::Cli::CmdRender` would compile identically and then fail at link with
an unresolved external, because the symbol is in no library. The archive
search in §2 is the evidence for that half, not this section.

Throwaway TUs (`ColdStart.cpp`, `ColdStartB.cpp`, `ColdStartC.cpp`, the 108
header-sweep TUs, and their `.obj`/`.bat`/`.log` files) were written to a
scratch directory outside the repository and deleted. Nothing entered the
tree. `git status` is clean; the full suite passes **48/48**.

---

## Bottom line for the v1.0 tag

The structural boundary is **real and holds**: zero private includes, zero
stubs, zero `-I Source` on any consumer target, 108/108 public headers
self-sufficient, and a from-scratch toolkit `main()` that compiles against
nothing but `Include/` and third-party. The examples are not passing by
accident — that was the specific hypothesis under test, and it is refuted.

What the tag is waiting on is **G1**: the SDK publishes a header
(`Include/Onyx/Cli/Render.h`) whose function ships in no library. A
third-party consumer who follows that header does not get a workaround —
they get an unresolved external and a 352-line source file to copy out of
`Examples/`. That is a shipped-surface defect, not a documentation one, and
it should be a scheduled SDK task before T9.

G2–G4 are real and worth fixing (G2 in particular makes the SDK feel far
smaller than it is), but none of them stops a determined consumer. G5–G6 are
hygiene.
