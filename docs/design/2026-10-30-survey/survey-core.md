# OnyxSDK domain-core survey (read-only)

Note: `hooks/code_tree.py` does not exist in the repo; surveyed with ls/grep/read. Paths relative to D:\CodingProjects\OnyxSDK, branch feat/host-ui-hooks.

## 0. Bottom line
The module contract (IGameModule / Workspace / DecoderRegistry / TypeRegistrar, ~500 LOC) is small, game-agnostic and works; OnyxBox proves image/text/scene + a mount. Everything around it is not generic: two parallel registration systems (one dead but still wired into UI), a hardcoded 3-viewer set, a data model shaped by God of War file formats, "Core" headers that drag UI icons, and a large share of the domain-core surface with zero callers (kept alive by the Onyx.h umbrella).

## 1. Layout
Source LOC: Modules 508, Container 101, Vfs ~380, Domain 44, Types 149, Schema 33, Api 48, Exchange 613, Audio 73, Viewers 2760 (VideoPlayer 990, Viewport3D 1153), Cli 1080, TestKit 463. Data model headers live in Include/Onyx/Parsers/ (header-only, no Source dir).
CMake layers (CMakeLists.txt ~480-790): Onyx_Core (glm + lz4_lib), Render, Shell (ImGui/glfw/miniaudio; contains Api/ToolkitApi.cpp, App/ViewerRegistry.cpp, ImageViewer, TextEditorViewer, Viewport3D, DocumentWindow), Media (FFmpeg, VideoPlayer), Exchange, CliRender, TestKit. A glob check (CMakeLists ~588) fatals if any Source/*.cpp is not assigned to a layer.

## 2. Genericity test
### 2a. OnyxBox / .obx / obx outside Examples/OnyxBox
Source/ and Include/ are clean except: Source/Cli/Commands.cpp:498 usage string hardcodes `onyxbox-cli`; comments naming the exe in Commands.cpp:213, Render.cpp:4,23,61, GltfExport.cpp:81, Camera.cpp:14, Viewport3D.cpp:43, Include/Onyx/Cli/{Gltf,Render}.h, Rendering/{Pipelines,VkContext}.h. CMakeLists.txt:469,704,834 (add_subdirectory Examples/OnyxBox). Tests: onyxbox_test, cli_test, mounts_test, testkit_test all include <OnyxBoxModule.h> -- every CLI/TestKit/Workspace test is coupled to the one example module; there is no second synthetic module. "Two modules coexist" is tested only with a decoy probe (cli_test.cpp:278-290). MinimalViewer Main.cpp:18,150 and OnyxCli register OnyxBox.
Verdict: the SDK does not hardcode OnyxBox. The leak runs the other way: the SDK was shaped by a real game.

### 2b. Game/format specifics baked into the SDK
- Source/App/App.cpp:425-432: recent-file label maps gameHint "gow1"/"gow2"/"ragnarok" to GOW1/GOW2/GOWR (shell hardcodes game names). Services/RecentFiles.h:13 same.
- Parsers/MeshData.h:16,20,21: MeshPart::textureLayer ("DMA instance/layer", PS2), isRigid ("GOWR: no BoneIdx/BoneWgt"), meshHash ("GOWR LOD-blob"). GltfExport.cpp:205 and GltfExport.h:117 branch on isRigid.
- Parsers/ObjectData.h:15-50: Joint has childsStart/End, externalId, invId, isQuaternion ("GOW1 only"), unkCoeff; ObjectData has matrixes1/2/3, vectors4..7 ("unknown, zero in most cases"), file0x20/file0x24. This is GoW's Object tag layout, not a generic skeleton; the renderer and glTF exporter consume it directly.
- Parsers/AnimationData.h:9-70: ANIM_DATATYPE_*, AnimSamplesManager, DataBitMap, howMany64kbSkip; comments "from anm.go", "mirrors JS stateDesc" -- port of god_of_war_browser's animation streams as the public SceneData::animations type.
- Parsers/SceneNode.h:15 BlendMode "parsed from mat.go flags"; :59-60 `flipZ = true` default ("GOW2 faces -Z"); :72 GOW2 appearance variants.
- Domain/MediaKind.h:13-24: closed enum with `Video // VPK, PSS, PSW`, `Audio // VAG, SBK`, `Container // Wad, Pak, Iso`; no Text kind (OnyxBoxModule.cpp:330 registers text as Unknown).
- Domain/Wad.h, Entry.h (wadName, "opened WAD"), App/Panels/WadStatsView.cpp: WAD naming through the domain model.
- Domain/MeshVertex.h: GpuVertex (boneWeights/uv1/tangent) is the CPU interchange vertex for every module; comment cites deleted rendering/GpuMesh.cpp glVertexAttribPointer (stale).
- Audio/AdpcmDecoder.*: PS2 VAG only, skips 0xC0 VPK fill; Onyx.h:45 calls it "IMA-ADPCM" (wrong); Onyx.h itself admits the SDK has no call site.
- Vfs/IsoFileSystem.cpp:~55: dual-layer ISO heuristic citing god_of_war_browser.
- Schema/AssetFormat.h:13 doc example `GOW2Material`; Types/TypeCatalog.h:12 example key "GOW2_MESH"; TypeCatalog::Register(info, forcedValue) exists to "preserve legacy enum values".
- Services/AssetVisibility, App/UIHelpers.h:18 "Role->visual mapping is GOWR-specific".

### 2c. Hardcoded viewer/format choice (registry bypassed)
- App/ViewerRouting.h:30 `enum class ViewerKind {None, Image, Text, Scene}`; ViewerRouting.cpp:7-10 fixed priority Scene>Image>Text. DecoderRegistry has only Scene/Image/Text slots (DecoderRegistry.h:44 "Audio and Schema slots arrive with first consumer").
- Source/App/Panels/DocumentBrowser.cpp:62-92 MakeShellViewerOpener() hardwires make_shared<ImageViewer>/<TextEditorViewer>/<Viewport3D>. Extension point = ViewerOpener (ViewerOpening.h:75-96) which is again exactly three typed callbacks.
- Examples/MinimalViewer/Source/Main.cpp:199-281 reimplements the opener and bolts on HexViewer by hand.
- Cli/Commands.cpp:285-345 re-derives Scene>Image>Text by hand; `--to` accepts only literal "gltf"; Render.cpp:300-330 hardcodes FOV 55, 1.2x margin, clear color, 6 views.
- App/ViewerRegistry.cpp:9-18: Image/Mesh/Audio/Video/Material all map to the same lambda.

## 3. Registry design
Three overlapping mechanisms:
(a) LIVE TypeCatalog (Types/TypeCatalog.*): global Meyer singleton `TypeCatalog::Get()` (cpp:6); string key -> uint32 TypeId (vector index; ids depend on registration order). TypeRegistrar::Add (TypeRegistrar.cpp:12-22) prefixes "<moduleId>." and rejects keys with '.'. No mutex, no unregister. Workspace takes the catalog by ref, but AppConfig.cpp:181,269 and TestKit/Goldens.cpp:127 read the global directly. TypeInfo carries icon/color (UI) and TypeCatalog.cpp:2 includes Fonts/SFSymbols.h (UI font in Core).
(b) LIVE DecoderRegistry: one per Workspace; 3 unordered_map<uint32_t, std::function<...>> keyed by TypeId. Typed per capability, but closed (adding a capability = editing SDK). Re-registration silently replaces (DecoderRegistry.h:46). DecodeScene/Image/Text are copy-pasted 3x with identical try/catch (DecoderRegistry.cpp:35-103). DecodeContext exposes the whole Document& (DecoderRegistry.h:17) so decoders reach into doc.fileTable and seek shared IFiles.
(c) DEAD-ish legacy Types::TypeRegistry + ITypeHandler + ONYX_REGISTER_FILE_TYPE (static-init self-registration into a second global singleton). ITypeHandler.h includes Fonts/SFSymbols.h and forward-declares Viewers/Schema/AssetContainer. Zero callers of RegisterByTypeId/ONYX_REGISTER_FILE_TYPE in Source/Examples; only Tests/types_registry_test.cpp. Yet:
   - ViewerRegistry (App/ViewerRegistry.cpp) is built on it: CanHandle = TypeRegistry::Resolve != null; Open = handler->CreateViewer. It has no Register method (ViewerRegistry.h:14-24) and nobody calls CanHandle/Open (only ToolkitApi holds a pointer). It would log "No viewer found" for every type.
   - Services/AssetVisibility.cpp:43 GetFilterableTypes() enumerates TypeRegistry::Get().AllHandlers() -> for any module-based app the type-visibility filter list is empty (functional bug, not just dead code).
   - README feature table says viewers "audio, material, hex ... registered through ViewerRegistry, opened by ViewerRouting": false. No audio or material viewer exists; hex is only in Examples/MinimalViewer; VideoPlayer exists but nothing opens it.
   - Onyx::Api (ToolkitApi.cpp) is a global-pointer facade (Config/Viewers/Documents; std::abort() if uninit, lines 20-45) used by DocumentBrowser.cpp:70-88 and CameraPanel; contradicts Workspace.h's "no singleton reach".
Lookup keys: TypeId from string key. No extension/magic registry. Module choice: Probe(path, 64 KiB header, size)->confidence 0..100; winner needs >=40 and strictly > runner-up (Probe.cpp:46-60). Mount choice: file extension string, first MountSpec wins (Workspace.cpp:106-125). --game matches id or hints[].
Single-module shortcut: Workspace.cpp:80-84 skips probing when exactly one module is registered: that module claims ANY file; adding a second module changes behaviour for the same file.
Coexistence: yes (namespaced keys, unique ids, duplicate id rejected Workspace.cpp:28-37). Unloading: no (AddModule only, no RemoveModule/unregister; Document::module is a raw IGameModule*). Dynamic loading: none; static link only. IGameModule uses std::string/std::function/std::vector/std::span in its ABI, no version handshake; consumption is FetchContent/add_subdirectory only (README).
Type safety: capability slots typed; type keys/module hints/diag codes/--to/view names are strings; ModuleState = shared_ptr<void> manual downcast (Workspace.h:27); OnyxBox decoders re-find their TOC entry by linear scan on (name,fileIndex) (OnyxBoxModule.cpp:83). Entry names are not unique but CLI looks up by first name match while the GUI uses positional NodePath (Selection.h) -> same name can resolve to different entries.

## 4. VFS / container
- IFile (Vfs/IFile.h): stateful cursor Read/Seek/Tell, `int origin`, no stateless ReadAt. A shared cursor makes concurrent decode unsafe: every decoder seeks the shared doc.fileTable[i]; parse runs on lane=docId, decodes on kDecodeLane=0 (ViewerOpening.h), so they can overlap. SliceFile::Read re-seeks the parent each call (SliceFile.h:25-37): two slices over one parent corrupt each other if interleaved.
- IFile::ReadAll (IFile.h:20-26) ignores short reads; used by OnyxBox MountObxPak to load the whole pak into RAM (OnyxBoxModule.cpp:189). MemoryFile takes vector by value.
- Streaming: reads are streaming (OsFile = std::ifstream), but extract/decode materialise whole entries: Commands.cpp ExtractEntries `std::vector<uint8_t> buf(e.source.size)` with size straight from the container (only bad_alloc catch protects), then a second write pass. No chunked copy, no mmap, no span/shared-buffer type: each layer copies.
- Nested archives: ByteRange{fileIndex,offset,size} (Domain/ByteRange.h) + Document::fileTable (slot 0 = root OsFile, 1+ = whatever the module pushes). No generic archive-in-archive resolver; module must stamp fileIndex itself (Workspace.h comment admits a miss is undetectable downstream).
- Root file is always `Vfs::OsFile(path.string())` (Workspace.cpp:95): a module cannot supply its own root. On Windows path.string() goes through the ANSI code page: non-ASCII path fails to open although RankProbes (Probe.cpp, ifstream(path)) just read it. IsoFile/OsFile also take std::string.
- IVirtualFileSystem: ListDirectory/OpenFile/Exists on std::string; no mount table, no overlay; one mountedVfs per Document selected by extension. IsoFileSystem::ListDirectory returns empty ("not fully implemented").
- IsoFileSystem bugs: ReadPVD and directory parsing ignore all Read return values; `*reinterpret_cast<uint32_t*>(&rec[2])` unaligned/endian UB; ParseDirectoryRecord recurses with no visited set or depth cap (self-referencing extent in a hostile ISO = stack overflow); rec(len) can overrun. No tests, no callers.
- lz4: lz4_lib (+xxhash.c) fetched and linked PUBLIC into Onyx_Core (CMakeLists.txt:77-93, 618); no source includes lz4.h (only a comment in GameModule.h and cli_test). No decompression IFile adaptor exists; TransformFile is a per-byte std::function (TransformFile.cpp:12-19), unusable for block codecs and slow.
- Container/Chunk{Reader,Schema,Tree}: clean IFF/RIFF-style walker over an in-memory span; tested; no SDK caller (module must ReadAll first).

## 5. Data model
Four names for overlapping jobs:
- Domain/: tree node (AssetEntry), byte location, MediaKind, GpuVertex, BoundingBox, Wad.
- Types/: identity (TypeId/TypeCatalog) + legacy handler registry + UI metadata (icon, color[4]).
- Schema/: two unrelated binary-layout DSLs. AssetFormat/AssetReader/AssetNode/DataTypes (header-only, richer DataType: Key/Asset/Enum/Pad/HexDump; AssetEntry::assetNode holds output; the inspector renders AssetNode) vs DataSchema/StructDef/NodeInstance (different FieldType enum; compiled into Core). grep: StructDef/NodeInstance/DataSchema referenced only by Onyx.h and each other, no test; AssetFormat/AssetReader referenced only by Onyx.h and each other. Nothing in SDK/Examples produces AssetNodes except the dead ITypeHandler::Parse. Schema/ is unused infrastructure.
- Parsers/: the real data model (SceneData, MeshPart, ObjectData, AnimationData, TextureData). Plain value types, only depends on glm and Domain; no Vulkan/ImGui. SceneData is move-only (vector<unique_ptr<TextureData>>). TextureData is "always RGBA8" yet has isCompressed/dataSize (half-finished); no mips/cube/array/HDR/sRGB. MaterialDesc has baseColor/blendColor/blendMode/uvOffset/metallic + map<TextureRole,int>; TextureRole enum is "appended, never reordered" (ABI by enum). No AudioData type anywhere.
UI coupling in "Core": ITypeHandler.h and TypeCatalog.cpp include Fonts/SFSymbols.h; Domain/MediaKind.cpp provides Icon(kind) as SF Symbol.

## 6. Dead / unreferenced surface (reachable only through Onyx.h umbrella)
No caller in Source/Examples/Tests: Schema/* (all), Vfs/IsoFileSystem+IsoFile, Vfs/SliceFile, Audio/AdpcmDecoder, Types/TypeRegistry+ITypeHandler+macro (test only), App/ViewerRegistry::Open/CanHandle + Api::Viewers(), Domain/Wad.h AssetContainer (vestige of the removed "profile" system, Wad.h comment), Container/Chunk* (tests only), lz4_lib, Viewers/VideoPlayer (990 LOC, never instantiated; CMakeLists ~770 admits "only consumers instantiate it"; zero tests). Onyx.h:~40-60 itself concedes AdpcmDecoder has no call site and four headers fail its own inclusion rule.

## 7. Viewers
- IDocumentContent: GetName/Draw/DrawInspector/IsOpen/SetOpen + GetEmbeddedViewport() returning concrete Viewport3D* (comment cites a nonexistent MapViewer). Immediate-mode ImGui, no lifecycle/capabilities. DocumentWindow tabs carry docId so closing a document closes its tabs (good).
- Opening path: OpenSelection -> RouteForType -> fully decode on JobQueue -> one of 3 callbacks. Hand-off is a fully materialised CPU object, so stream-type content (audio/video) cannot fit; VideoPlayer takes shared_ptr<IFile> directly and is wired by consumers outside this path.
- Duplication: ImageViewer.cpp:100-240 has its own toolbar, animated zoom/pan, texture upload via TexturePool; Viewport3D and VideoPlayer (transport UI :728-960) each have their own toolbars; no shared toolbar/zoom/error widget. A failed decode only logs a warning; the GUI shows no error tab (diags are discarded unless the inspector reads them).
- All viewers use global Rendering::GetGlobalContext() (ImageViewer.cpp:52, VideoPlayer.cpp:612, Viewport3D.cpp:201,230,318); Window.cpp:232-239,414 comments record use-after-teardown bugs "caught live".
- VideoPlayer.cpp (990): AVIO custom IO over IFile (:112-130), 3 std::thread (demux/video/audio, :254-256) with mutex/condvar queues, AudioRingBuffer (:54-95), miniaudio device, A/V clock sync, SAR/aspect (:171-190, GoW PSW/PSS comment), TexturePool/VkContext upload (:610-645), full ImGui transport UI (:728-960). Header includes libav*, miniaudio.h, imgui.h (hence its own Media.h umbrella). Does not belong in an SDK core: split into a UI-free MediaSource (frames+PCM, headless-testable, usable by CLI) and a thin view, or ship it out of tree. Currently no decoder slot, no route, no test.
- Tests: only DocumentWindow and Viewport3D::LoadScene bounds. ImageViewer/TextEditor/VideoPlayer untested.

## 8. CLI vs GUI
Same: both use Workspace (Open/OpenAsync share RunParse) and DecoderRegistry. So "same code path" holds for parse+decode only.
Duplicated/drifting glue:
- Routing: CmdDecode hand-codes HasScene/HasImage/HasText (Commands.cpp ~285-345) with a comment "mirrors RouteForType exactly (CLI and GUI routing must not diverge)" -- RouteForType lives in Shell so Core cannot call it; enforced by nothing.
- Entry lookup: first-name-match DFS (Commands.cpp FindEntryByName, copied into Render.cpp:100-110 with a comment justifying the copy) vs GUI NodePath.
- Duplicated Commands.cpp vs Render.cpp: JsonEscape, SeverityLabel, PrintDiags, AnyError/AnyErrorDiag, FindEntryByName; hand-rolled JSON (render's sidecar .json by string concat, Render.cpp ~395).
- Camera framing: Render.cpp ~310-330 hand-rolls framing and admits it is "close to but NOT byte-identical to Camera::FocusOn" (diverges for sub-unit bboxes): CLI render and GUI frame the same scene differently.
- CmdExtract (CLI only; no GUI extract) always returns kOk even when entries error/short-read; CmdList ignores Failed; only decode/render honour --strict. CmdDecode returns kOk if the decoder returned null with no Error diag. Run(): unknown --flags become positional args; trailing --game silently dropped; atoi for --width/--height unvalidated; magic exit 77 outside the kOk/kUsage constants; usage text says "onyxbox-cli".
- Layering workaround: Core cannot link Exchange/Render, so export/render are injected as SceneExportFn/RenderFn std::function hooks into Run(); real impl lives in Examples/OnyxCli/Gltf.cpp or Onyx_CliRender; Include/Onyx/Cli/Gltf.h ships in no library. A third party must copy Examples/OnyxCli/Main.cpp wiring to get a CLI.
- All commands open synchronously; no cancel/progress.

## 9. TestKit + Tests
TestKit (463 LOC): Goldens (SnapshotTree, uses global TypeCatalog::Get() at Goldens.cpp:127), DecodeSmoke, RenderCompare (stb_image). Core-only, headless, tested by testkit_test.cpp (405 LOC) against OnyxBox.
Tests/: 56 files, 283 TEST_CASEs, ~8.5k LOC, 438 KB total. Golden/corpus = 5 render scenes (json+png each) ~437 KB; container fixtures are synthesised at runtime in temp_directory_path(). Consumer/ (21 KB) = external-consumption configure/build test. Largest: cli_test 1115, mounts_test 905, workspace_test 590, viewerrouting_test 575, oracle_corpus_test 567.
CMake smell (Tests/CMakeLists.txt:77-117): `OnyxUnit` runs everything, then ~40 more ctest entries re-run subsets by doctest name-glob filters: everything runs twice; renaming a TEST_CASE silently drops it from its named suite.
Tested layers: Workspace/Probe/Selection/DecoderRegistry/ViewerRouting/ViewerOpening, TypeCatalog/Registrar/Registry, mounts+extract, Chunk*, TransformFile, CLI, GltfExport (round-trip), TestKit, Vulkan render (needs GPU/lavapipe), Jobs/EventBus/Settings etc.
Untested: IsoFileSystem/IsoFile, SliceFile, AdpcmDecoder, Schema (all), Api/ToolkitApi, ImageViewer, TextEditor, VideoPlayer, MediaKind Name/Icon (incidental). No hostile-input tests for anything but OnyxBox. No test with two real modules, module unload, or concurrent decode on one Document. All genericity evidence is one invented format.

## 10. Exchange/GltfExport
Standalone: includes only Parsers/{Mesh,Object,Texture}Data, Domain/MeshVertex, cgltf_write, stb_image_write, glm; Onyx_Exchange links Core only (CMakeLists.txt:650). Entanglements: re-derives skinning from GoW's Q.14 idle rotation (ObjectData.vectors5) and must mirror Rendering/JointPalette.cpp ("oracle it must not diverge", GltfExport.cpp:~40); branches on MeshPart::isRigid (:205) and a GOW2 Z-flip (:506); STB_IMAGE_WRITE_IMPLEMENTATION defined in both GltfExport.cpp and Cli/Render.cpp (ODR workaround in comments). Animation: grep for "animation" in GltfExport.cpp returns nothing -> skin + rest pose only, no animation export. Only glTF.

## 11. Bug / maintenance smells
- Global singletons: TypeCatalog::Get, TypeRegistry::Get, AssetVisibility::Get, Api:: pointers, GetGlobalContext. Workspace takes the catalog by ref but the SDK reads the global elsewhere, so a second Workspace/catalog silently disagrees with persisted settings.
- TypeCatalog not thread-safe; Workspace::Get linear scan and returns raw Document* across threads; Document::module raw pointer.
- 17 catch(...) sites in Source/Examples/Include, none empty; Probe.cpp:71 collapses unreadable file and no-module into the same empty ranking; Workspace::Open returns 0 for both "no module" and "cannot open".
- Ignored returns: IsoFileSystem (all reads), IFile::ReadAll, SliceFile ctor Seek; signed/unsigned mixing in AssetReader::Parse (size_t vs int64_t); IsoFile size_t (32-bit on 32-bit builds) vs 64-bit ByteRange; NodeInstance::GetValue throws std::out_of_range in a no-throw-contract SDK.
- Every module must re-implement bounds/short-read checks: OnyxBox ~150 lines for 3 trivial decoders (OnyxBoxModule.cpp:570-724); no helper in the SDK.
- Stale/misleading comments: MeshVertex.h (GpuMesh/GL), Wad.h/Entry.h ("WAD"), GameModule.h forward decls "Task 3"/"Task 2", public headers carry long milestone/spec narration (e.g. DecoderRegistry.h's 8-line note on wingdi.h TextOut macro, caused by Services/PathUtils.h pulling <windows.h> into Onyx.h). Portuguese comments in Vfs/IFile.h, IVirtualFileSystem.h, SliceFile.h, MemoryFile.h, NodeInstance.h, StructDef.h.
- Magic numbers: exit 77, kDecodeLane=0, 64 KiB probe, 256 KiB AVIO buffer, FOV 55/1.2x.
- Only one TODO in scope (Include/Onyx/Rendering/VkContext.h:107); debt is documented in prose (CHANGELOG.md, 1361 lines, "Known gaps"), so TODO grep under-reports.
- ParseResult is a bare bool (no partial/cancelled/unsupported distinction); ModuleInfo has no version/capabilities.

## 12. Heaviness drivers
Core drags glm + lz4 (unused) + xxhash + tomlplusplus include dir to every layer (onyx_apply_common ~605); Shell drags ImGui/implot/TextEditor/glfw/miniaudio; Media drags FFmpeg (Windows archive downloaded at configure); Render volk+VMA. 7 static libs + INTERFACE aggregate + 6 sibling umbrella headers whose inclusion rules need ~90 lines of comment in Onyx.h; the layering exists mostly to prevent link errors from public headers that ship in no library (Cli/Gltf.h). docs/ contains 12 milestone plans + 3 design docs (process artefacts).

## 13. Worth preserving
IGameModule + ProbeRanking + id-namespaced TypeRegistrar + typed DecoderRegistry + DiagSink-not-exceptions; NodePath selection; Workspace async open/close race handling and destruction-order contract; GltfExport layering; TestKit.

## 14. Refactor targets (ranked)
1. Collapse (a)(b)(c) into one capability registry per module (TypeId -> {decoders, viewer factory, icon/color}); delete TypeRegistry/ITypeHandler/ViewerRegistry/Api facade; fix AssetVisibility to read the catalog; make OpenSelection and CLI use one Resolve(typeId) instead of three Has*() chains.
2. Open the capability set (add Audio/Table/Raw/Text kinds, open ViewerKind) or let modules register viewer factories.
3. Move GoW-shaped fields (isRigid, meshHash, textureLayer, anm stream types, Joint file layout, flipZ default) out of Parsers/MediaKind into a neutral skeleton/animation model or module extension data.
4. Delete dead code: Schema/StructDef+NodeInstance+DataSchema, IsoFileSystem/IsoFile, AdpcmDecoder, lz4_lib (or add a real codec adaptor), AssetContainer/Wad.h, unused AssetFormat/AssetReader/SliceFile.
5. Stateless IFile::ReadAt(offset, span); streaming extract; wide-path open on Windows.
6. Split VideoPlayer into UI-free source + view or move out of tree.
7. One Core-level CLI/GUI glue layer (route, entry lookup, camera framing, diag/JSON printing); CmdExtract should fail on errors.
8. Injectable-only catalog; thread safety; RemoveModule or document process-lifetime modules.
9. Add a second synthetic module and hostile-input tests; fix the ctest double-run.
