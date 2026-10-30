# OnyxSDK public API surface survey (Include/Onyx, 117 headers)

## 0. Step 1 raw output
`python D:\CodingProjects\OnyxSDK\hooks\code_tree.py --central` and `--cycles` both returned: `can't open file 'D:\CodingProjects\OnyxSDK\hooks\code_tree.py': [Errno 2] No such file or directory`.
There is no hooks/ directory in the repo (find for code_tree* is empty). I substituted an equivalent script
(scratchpad/graph.py; only real line-start #include directives counted, comments excluded). Raw output:

```
== fan-out >=10 (direct Onyx includes)
54 Onyx/Onyx.h
13 Onyx/Render.h
9 Onyx/Modules/Workspace.h
8 Onyx/Viewers/Viewport3D.h
== fan-in top 20 (direct)
10 Onyx/Vfs/IFile.h
9 Onyx/Types/TypeId.h
9 Onyx/App/IPanel.h
9 Onyx/Parsers/SceneNode.h
8 Onyx/Modules/Workspace.h
6 Onyx/Viewers/IDocumentContent.h
5 Onyx/Rendering/RenderBatch.h
5 Onyx/Types/TypeCatalog.h
5 Onyx/Parsers/TextureData.h
4 Onyx/Domain/MediaKind.h
4 Onyx/Modules/Selection.h
4 Onyx/Rendering/Camera.h
4 Onyx/Domain/Entry.h
4 Onyx/Rendering/VkResources.h
4 Onyx/Parsers/AnimationData.h
3 Onyx/Rendering/AxisGizmo.h
3 Onyx/Services/AppConfig.h
3 Onyx/Rendering/AnimationPlayer.h
3 Onyx/Cli/Commands.h
3 Onyx/Services/Logger.h
== transitive closure size top 15
81 Onyx/Onyx.h
38 Onyx/App/Window.h
35 Onyx/App/App.h
26 Onyx/App/ViewerOpening.h
25 Onyx/CliRender.h
24 Onyx/Cli/Gltf.h
24 Onyx/Cli/Render.h
23 Onyx/Cli/Commands.h
23 Onyx/Modules/DecoderRegistry.h
20 Onyx/Render.h
19 Onyx/TestKit.h
19 Onyx/App/Panels/InspectorPanel.h
18 Onyx/App/UIHelpers.h
18 Onyx/App/ViewerRegistry.h
18 Onyx/App/Panels/DocumentBrowser.h
== who transitively pulls SFSymbols.h
8
['Onyx/Onyx.h', 'Onyx/App/App.h', 'Onyx/App/TypeVisuals.h', 'Onyx/App/UIHelpers.h', 'Onyx/App/ViewerRegistry.h', 'Onyx/App/Window.h', 'Onyx/Types/ITypeHandler.h', 'Onyx/Types/TypeRegistry.h']
== direct includers of SFSymbols.h / Fonts
['Onyx/App/TypeVisuals.h', 'Onyx/Types/ITypeHandler.h']
== cycles (SCC>1): 0
== folder deps
(root) -> ['Api', 'App', 'Audio', 'Cli', 'Container', 'Domain', 'Exchange', 'Modules', 'Platform', 'Rendering', 'Schema', 'Services', 'TestKit', 'Types', 'Vfs', 'Viewers']
App -> ['Domain', 'Fonts', 'Modules', 'Parsers', 'Rendering', 'Services', 'Types', 'Viewers']
Cli -> ['Modules', 'Parsers']
Domain -> ['Schema', 'Types']
Exchange -> ['Parsers']
Modules -> ['Domain', 'Parsers', 'Services', 'Types', 'Vfs']
Parsers -> ['Domain']
Rendering -> ['Domain', 'Parsers']
Schema -> ['Vfs']
Services -> ['Types']
TestKit -> ['Modules']
Types -> ['Domain', 'Fonts', 'Parsers', 'Vfs']
Viewers -> ['Domain', 'Parsers', 'Rendering', 'Vfs']
```

Header-level: 0 include cycles. Folder-level cycle: Domain -> Types (Domain/Entry.h:18 TypeId.h), Types -> Parsers (Types/ITypeHandler.h:2 SceneNode.h), Parsers -> Domain. Also Domain -> Schema (Entry.h:17), so Domain is not a leaf.

## 2. Inventory (headers per folder)
App 23, Services 18, Rendering 13, Schema 7, Vfs 7, Domain 6, Viewers 6, Modules 5, Parsers 5, Types 5, Cli 3, Container 3, TestKit 3, Fonts 2, Api 1, Audio 1, Exchange 1, Platform 1, plus 7 root umbrellas (Onyx.h, Render.h, Media.h, CliRender.h, Exchange.h, TestKit.h, Version.h).
Include/ is organised by subsystem, NOT by CMake layer (Core/Render/Shell/Media/Exchange/CliRender/TestKit). Same folder mixes layers:
- Services/: Logger/Jobs/EventBus/Settings (Core) beside Appearance.h and ThemeManager.h (include imgui.h; their .cpp are in ONYX_SHELL_SOURCES with ScaleManager/AppConfig).
- Rendering/: AxisGizmo.h includes imgui.h (its .cpp is in the Shell list); Rendering/TexturePool.h AND App/TexturePool.h both exist.
- App/ (23 headers): Shell internals (Widgets.h, UIHelpers.h, TypeVisuals.h, InfoTab*.h, Panels/*) are all public. Onyx.h (~L75) admits they are "Shell-INTERNAL detail headers" yet they stay public.
Contract-grade: Modules/{GameModule,Workspace,DecoderRegistry,Probe,Selection}.h, Types/{TypeId,TypeCatalog,TypeRegistrar}.h, Vfs/*, Parsers/* (domain data), Services/{EventBus,Jobs,Diagnostics,Settings}.h, App/{App,IPanel}.h, Viewers/IDocumentContent.h.
Impl-detail / legacy leaked: Types/{ITypeHandler,TypeRegistry}.h, Domain/Wad.h, Api/ToolkitApi.h, Services/{EventManager,Events,TaskManager,SessionLog}.h, App/{Widgets,UIHelpers,TypeVisuals,TexturePool,Panels/*}.h, Fonts/{SFSymbols,IconTable}.h, Rendering/{VkContext,Pipelines,VkResources,RenderContext}.h.

## 3. Third-party leaks in Include/
| Header | Leaks | Justified? |
|---|---|---|
| App/App.h, Window.h, WindowDecorator.h | imgui.h, GLFW/glfw3.h | Shell: partly (ImGuiID in LayoutFn App.h:46; GLFWwindow* in Window). Leaks GLFW to every App.h consumer |
| App/{TexturePool,TypeVisuals,Widgets}.h | imgui.h | Shell internals, should not be public |
| Services/{Appearance,ThemeManager}.h | imgui.h | Services should be UI-free per spec |
| Rendering/{RenderContext,VkContext}.h | volk.h, vk_mem_alloc.h, vulkan.h | Justified as "raw floor"; Render.h pulls all |
| Rendering/AxisGizmo.h | glm + imgui.h | Not justified: renderer header depends on ImGui (Render.h comment admits it was undocumented) |
| Viewers/{ImageViewer,Viewport3D}.h | imgui.h | Shell, acceptable |
| Viewers/VideoPlayer.h (254 lines) | libavformat, libavcodec, miniaudio, imgui | NOT justified: FFmpeg/miniaudio in a public header; ~37 methods; ThreadSafeQueue template defined inline (L30-80). Needs pimpl |
| Domain/{BoundingBox,MeshVertex}.h, Parsers/{ObjectData,SceneNode}.h, Rendering/{AnimationPlayer,Camera,JointPalette,Pipelines,RenderToImage,SceneRendererVk}.h | glm | glm is Core's PUBLIC dep (CMake L618); defensible but welds consumers to glm |

Services/PathUtils.h:9-10 defines WIN32_LEAN_AND_MEAN and includes windows.h; reached via Onyx.h so every consumer gets windows.h macros (DecoderRegistry.h comment: TextOut macro collision forced rename to DecodedText).

## 4. Largest headers / compile weight
Fonts/SFSymbols.h 6686 lines = 41% of the 16378 lines in all headers. Next: SceneRendererVk.h 428, Pipelines.h 382, VideoPlayer.h 254, AssetFormat.h 248, Workspace.h 241.
SFSymbols.h is included directly by Types/ITypeHandler.h:5 and App/TypeVisuals.h. Transitive includers (8): Onyx.h, App/App.h, App/Window.h, App/ViewerRegistry.h, App/UIHelpers.h, App/TypeVisuals.h, Types/TypeRegistry.h, Types/ITypeHandler.h. So App.h and Onyx.h (the common entry points) parse 6.7k lines of icon constants, dragged in by the LEGACY ITypeHandler. Closure sizes: Onyx.h 81 of 117 headers; Window.h 38; App.h 35.
Most inline bodies (rough count): Services/EventManager.h 35 (macro-generated, static recursive_mutex at L194-196), Schema/AssetFormat.h 29, Rendering/AnimationPlayer.h 24, Schema/StructDef.h 12, Schema/AssetReader.h 11, Rendering/VkContext.h 10, App/App.h 10, Viewers/VideoPlayer.h 8, Services/PathUtils.h 8.

## 5. God headers/interfaces (heuristic counts)
- Method counts: VideoPlayer.h ~37; EventManager.h ~35; TaskManager.h ~34 (+11 std::function); App/Window.h ~30 (Vulkan state, platform, decorator in one class; member order is a documented construction/destruction contract ~L131); AnimationPlayer.h ~28; Schema/AssetNode.h ~24; App/App.h ~24.
- App.h grows one std::function hook per commit (SetRegistrar L41, SetDefaultLayout L46, SetOpenFileHandler L67, SetMenuEntryFilter L87) with feature-detection macros ONYX_HAS_OPEN_FILE_HANDLER (App.h:18), ONYX_HAS_MENU_ENTRY_FILTER (App.h:21), ONYX_HAS_DOCUMENT_WINDOW_VISIBILITY (Viewers/DocumentWindow.h:11): API grown by patch.
- Direct Onyx include fan-out: Onyx.h 54, Render.h 13, Modules/Workspace.h 9, Viewers/Viewport3D.h 8. Workspace.h L2-10 drags Probe, Diagnostics, EventBus, Jobs, Settings, TypeCatalog, Entry, IFile; DecoderRegistry.h:2 includes all of Workspace.h only for Document.
- Workspace (Workspace.h:113-233) = modules + open/probe + events + jobs + settings + decoders + catalog; Document has ~10 fields and its thread contract only in a comment (L36-39).
- Highest fan-in: Vfs/IFile.h 10, Types/TypeId.h 9, App/IPanel.h 9, Parsers/SceneNode.h 9.

## 6. API/ABI hygiene
Singletons/globals in public headers: Types/TypeCatalog.h:22 TypeCatalog::Get() (102 call sites repo-wide), Types/TypeRegistry.h:18 (11), Services/Logger.h:68 Logger::Get() (170 sites incl. macros), Services/AppConfig.h:93 AppConfig::Get() nullable (7), Services/AssetVisibility.h:31 (11), Services/Appearance.h:106 free Get(), App/Panels/CameraPanel.h:17-21 static s_instance, Api/ToolkitApi.h ("Global singleton facade", 3 raw pointers; Api::Documents() used in DocumentBrowser.cpp:70-93, CameraPanel.cpp:53), EventManager.h:194 static recursive_mutex.
Arch doc 5.4 says retired singletons: ProfileManager, TypeCatalog::Get, TypeRegistry::Get, AssetVisibility::Get, AppConfig::Get. All still present; TypeCatalog::Get is the most-used. Workspace ctor takes TypeCatalog& (Workspace.h:117), which in practice is the singleton.
Raw pointers: VideoPlayer.h 7, AnimationPlayer.h 5, App.h 3, ToolkitApi.h 3, Workspace.h 2 (Document::module non-owning, L77). No new/delete in public headers (clean).
std::function soup: TaskManager.h 11, ViewerOpening.h 5, App.h 4, TexturePool 3, DecoderRegistry.h 3 (L31-33), Jobs/EventManager/EventBus/RenderContext/Cli/Commands.h 2 each.
Macros: ONYX_REGISTER_FILE_TYPE (Types/ITypeHandler.h:71, static-init self-registration into the TypeRegistry singleton); EVENT_DEF/EVENT_DEF_IMPL (EventManager.h:25-62); three logging macro families LOG_*, ONYX_LOG_*, ONYX_LOGF_* (Logger.h:138-169); ASSERT_MAIN_THREAD (Threading.h:35-37).
Global-namespace pollution: App/UIHelpers.h:10-13 `using Onyx::App::HashHex...` at global scope; Domain/Entry.h trailing global `using AssetEntry`; global `namespace PathUtils` (audit G5).

## 7. Docs vs reality
Promised (v1 arch): sec 3 Core (no UI/GPU) / Render (Core only) / Shell, "enforced as separate CMake targets"; 5.4 Workspace is composition root replacing every registry singleton; 6 ITypeHandler retired; 7.5 no resource-owning singletons; 13 no game name in Core; 15 public surface = Include/Onyx/** except Include/Onyx/Detail/** - there is NO Detail/ directory, so all 117 headers (EventManager, Widgets, ...) are SemVer-public.
Violations today:
1. Targets exist (CMakeLists.txt L615-795) but Include/ is not split per target; membership is by hand-maintained .cpp lists with a FATAL_ERROR completeness glob (~L568-578). LayerGuard (L868-905) greps only `imgui|implot|GLFW/|glad/`, only Core and Render, only over files with a same-named .cpp/.h pair. Shell has no rule. Headers without a same-named .cpp escape it.
2. Legacy v0 path still alive: App/ViewerRegistry.cpp:6-30 routes through TypeRegistry::Get().Resolve -> handler->CreateViewer(entry, AssetContainer&); Domain/Wad.h:3-12 says AssetContainer "survives" for ViewerRegistry/ITypeHandler. Meanwhile the new path is App/ViewerRouting.h RouteForType(DecoderRegistry&, TypeId) (Scene>Image>Text). Two viewer-dispatch systems.
3. Two event systems: Services/EventManager.h+Events.h (macro, global, used only by Source/App/Window.cpp) vs Services/EventBus.h (Workspace-owned). Two job systems: TaskManager.h (App.cpp, StatusBar.cpp, Window.cpp) vs Jobs.h (Workspace, CLI, ViewerOpening, DecodeSmoke).
4. Two TexturePools (App/ and Rendering/).
5. imgui in Services/ (Appearance, ThemeManager) and Rendering/ (AxisGizmo): the W1 "imgui out of Services/Rendering" gate is not met.
6. Onyx.h is 191 lines: 54 includes + ~60 lines of curation prose; it is the only thing curating the public surface; 81/117 reachable (audit measured 49/108 at da7da07). Audit gaps G2/G5 (windows.h, global namespaces) still visible; G3 (Version.h) now exists in the tree; G4 (no install()/find_package) not rechecked.
7. The "108/108 compile standalone" audit proves self-sufficiency only, not layering.

## 8. Module contract: game-agnostic?
IGameModule (Modules/GameModule.h:44-57) is small and clean: Info, Probe, RegisterTypes, RegisterDecoders, Mounts (default empty), ParseContainer. ParseResult is just {bool ok} (L40); the spec's AssetTree result became side-effect writes into Document.roots (Workspace.h:74-107).
Gaps and game bleed:
- DecoderRegistry (DecoderRegistry.h:35-55) has Scene/Image/Text only; Audio and Schema "deliberate YAGNI" (L52) though spec 6 lists them and the Shell ships VideoPlayer and audio. Audio/video/schema content cannot be module-driven.
- No module hook for viewers/panels: custom UI goes via App::SetRegistrar(std::function<void(App&)>) (App.h:41), i.e. the host executable, not the module. Module != toolkit; a second-game author edits main().
- Domain::MediaKind (Domain/MediaKind.h:9-23) is a closed 13-value enum (Mesh, Material, Skeleton, Animation, Script, Map, Shader, Container...) with PS2-format comments ("VAG, SBK", "VPK, PSS, PSW"). TypeInfo.media (Types/TypeCatalog.h:12) must choose from it; a new kind is an SDK edit. Spec said Scene|Image|Audio|Video|Text|Blob.
- Game names in SDK code: Parsers/MeshData.h:20-21 (isRigid "GOWR", meshHash "GOWR LOD-blob"), Parsers/ObjectData.h:27 ("GOW1 only; GOW2"), Parsers/SceneNode.h:59,72 (GOW2 axis flip, GOW2 appearance variants), Services/RecentFiles.h:13 gameHint "gow1/gow2/ragnarok", Source/App/App.cpp:425-432 hard-coded gow1/gow2/ragnarok -> "GOW1/GOW2/GOWR" labels, Source/App/Platform/macos_menu.m:15 GOWMenuItemHandler, App/UIHelpers.h:18 ("ColorForRole/IconForRole is GOWR-specific"), Exchange/GltfExport.h:117 GOWR. Violates spec 13. Domain structs carry per-game fields and have no extension slot.
- TypeRegistrar is module-bound but writes into the TypeCatalog singleton (TypeRegistrar.h:11); AssetVisibility singleton persists by key.
- Probe/ParseContainer/Mounts are decoupled; host supplies the 64 KiB header.
- Spec 13 generality proof (second toolkit, zero SDK edits) was cut; replaced by an audit proving reachability only. OnyxBox is the only non-GoW consumer and is a toy.
New module author must touch outside the module: (a) MediaKind enum for kinds outside the 13; (b) main()'s App registrar for any custom viewer/panel or non-Scene/Image/Text content; (c) possibly Parsers structs (MeshData/SceneNode) to carry format-specific data, which is how GoW fields got in; (d) the global TypeCatalog ordering in tests; (e) windows.h macro collisions via PathUtils.

## Ranked top findings
1. Two parallel architectures in one public API (legacy ITypeHandler/TypeRegistry/ViewerRegistry/AssetContainer/ToolkitApi vs IGameModule/Workspace/DecoderRegistry); duplicate EventManager/EventBus, TaskManager/Jobs, TexturePool x2.
2. Singletons the spec retired are the dominant access path (TypeCatalog::Get 102 sites, Logger 170).
3. Public surface == entire Include tree (no Detail/): 117 headers, 41% is an icon table, Shell internals and Vulkan/FFmpeg/ImGui/GLFW/windows.h leak.
4. Include/ layout != CMake layers; LayerGuard is a narrow grep.
5. GoW-specific names/fields in domain structs and Shell code.
6. DecoderRegistry and MediaKind closed; modules cannot contribute viewers.
7. God classes: Window, App, VideoPlayer, EventManager, TaskManager, Workspace.
