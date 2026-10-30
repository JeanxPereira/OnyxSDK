# OnyxSDK cross-cutting bug / quality survey (static, read-only)

Scope: Source/ and Include/ (SFSymbols.h and third_party excluded). Nothing was built or run. Everything below was read, not executed, so "triggers" are reasoned from the code. Line numbers are approximate (+-3) where marked with ~.

Overall: comment density is very high and the Workspace/Jobs/EventBus layer is defensively written. The real defects cluster in four places: VideoPlayer threading/lifetime, the Vulkan "OneShot = blocking submit" pattern (main source of "heavy"), the ISO9660 parser (untrusted input), and process-wide singletons/statics.

## 1. Markers
Very few. Real ones:
- Include/Onyx/Rendering/VkContext.h:107 `TODO(later task): if a caller ever needs to read ...`
- Source/Cli/Render.cpp:316 `left hand-rolled for now` (camera framing duplicates Camera::FocusOn; comment admits the formulas diverge for radius < 1).
- Source/App/ViewerOpening.cpp:87 `Default-constructed (empty/no-op) for now` (benign).
- Source/App/Window.cpp:140 `Temporary-but-committed log lines` (debug logging left in).
- Source/App/AnimationTimeline.cpp:26 `For now, plant ticks at frame 0 and last frame` (timeline ticks are a stub).
- Source/App/Platform/Window_windows.cpp:26, Window_linux.cpp:21,26 `No-op ... for now`.
- Include/Onyx/App/UIHelpers.h:42 `the workaround (pick a file inside it, reduce to the parent)` (folder-picker design note).
- Self-declared dead/stub code with no marker: Viewport3D::LoadFromMeshData (Source/Viewers/Viewport3D.cpp ~236, "Dead API ... zero callers", logs and clears), ImageViewer compressed-texture branch (ImageViewer.cpp ~48), IsoFileSystem::ListDirectory (IsoFileSystem.cpp ~140, returns empty).

## 2. Swallowed errors
catch(...) sites: Window.cpp:907, Commands.cpp:280, DecoderRegistry.cpp:45/69/93 (report a Diag, fine), Probe.cpp:19/87, Workspace.cpp:166/186 (report Diag, fine), RenderContext.cpp:53 (logs), EventBus.cpp:85 and Jobs.cpp:182/226 (log), Logger.cpp:196 (silent, intentional), Settings.cpp:68 (returns false), TaskManager.cpp:191 (logs + marks task).
Problem ones:
- **Window::fullFrame (Source/App/Window.cpp:894-912)**: in non-Debug builds wraps frameBegin/frame/frameEnd in `catch(...)` that prints a fixed string to stderr (no what(), not through Logger), calls `ImGui::EndFrame()`, keeps going for up to 10 consecutive frames, then abort(). Debug builds have no try at all, so Debug and Release behave differently. An exception thrown after `vkResetFences` in presentFrame leaves the fence unsignalled; next `vkWaitForFences(UINT64_MAX)` (Window.cpp:1007) hangs forever. An exception inside an ImGui Begin/End pair leaves the ImGui stack unbalanced.
- **Settings::Load (Settings.cpp:47-52)** swallows `toml::parse_error`, returns empty Settings; a later Save() overwrites the user's (possibly just hand-edit-broken) file: silent data loss. Same in **AppConfig::load (AppConfig.cpp:82-86)**: parse error returns defaults, next save() clobbers the file. No backup, no log.
- **Window::presentFrame (Window.cpp:1007, 1026, 1029, 1037, 1133)**: vkWaitForFences, vkResetFences, vkBeginCommandBuffer, vkEndCommandBuffer results ignored. vkQueueSubmit2 failure (~1118) is logged but execution continues to vkQueuePresentKHR and the fence stays reset, so the next frame deadlocks. VK_ERROR_DEVICE_LOST never handled.
- VideoPlayer: `av_seek_frame` result ignored (VideoPlayer.cpp ~371), `swr_init` ignored (:226), `ma_device_start` ignored. `AVSEEK_FLAG_BYTE` fails on many containers (mp4/mkv) so seek silently does nothing while the UI slider already jumped.
- TextEditorViewer::ExportAs (~48): `out.write` unchecked; "Exported" logged even on full disk.
- RecentFiles::Load (RecentFiles.cpp ~31): `fs::exists(p)` is the throwing overload (bad path = uncaught exception at startup); Save() ignores failure; paths containing TAB/newline corrupt the line format.
- AppConfig::save (AppConfig.cpp:195-290): truncates with `std::ofstream` and writes in place: crash mid-write = empty config. Silent `return` if open fails.
- IsoFileSystem ReadPVD / ParseDirectoryRecord: no `file.Read` return value is checked.
- `assert` for runtime validation: effectively none (RenderToImage.cpp:34 converted one to a runtime check). SceneRendererVk.cpp:540 asserts `proj[1][1] < 0` but the public entry validates at runtime. `ASSERT_MAIN_THREAD()` (Services/Threading.h:37) is defined, never used in Source/.
- `(void)` casts are only unused-param silencing; Dopesheet.cpp:226 `(void)colBg` marks a dead variable.

## 3. Ownership / lifetime
- No raw `new`/`delete`. Only malloc/free around file-dialog C strings (SystemFileDialog.cpp:80,106) and `std::free(png)` (GltfExport.cpp:413). Good.
- **VideoPlayer packet queues leak**: `ThreadSafeQueue<AVPacket*>` (VideoPlayer.h:181-182); `clear()` (VideoPlayer.h:57) pops raw `AVPacket*` without `av_packet_free`. Called from SeekByte (VideoPlayer.cpp:341-355) on every seek/scrub; Close() (:268) never drains the queues either.
- **VideoPlayer::Close() not idempotent**: `sws_freeContext(m_swsCtx)` (VideoPlayer.cpp:297-298) does not null the pointer (other contexts use `_free(&p)` forms that do). Constructor calls Close() on init failure (~108) and the destructor calls it again; explicit Close() then dtor likewise: double free of the SwsContext whenever a video decode created one.
- **EventManager::post (EventManager.h:160-174)** iterates `multimap::equal_range` while invoking handlers under a recursive_mutex. A handler that calls `unsubscribe` (itself or a peer on the same event) erases the node being iterated, then `++it` = UB. `call()` rethrows after logging, so one throwing handler aborts delivery to the rest and propagates into the poster.
- Cycle handled: Jobs.cpp:193 nulls `job->work` after run. The unhandled twin: ViewerOpening.cpp subscribes `DocumentClosed` with a lambda capturing `handle` (JobHandle -> JobState -> onDone -> subBox -> Subscription -> bus -> lambda -> handle); broken only when Done runs. For jobs queued but never started (shutdown) `JobQueue::~JobQueue` nulls only `work` (Jobs.cpp:108-112), so onDone keeps docShared/subBox/placeholder alive (shutdown-only leak, keeps Document file handles open).
- `entry` is a raw `const AssetEntry*` into `doc->roots`, captured by ViewerOpening worker lambdas (~105,~112). Document is kept alive by docShared; latent if a reparse path ever mutates roots.
- `[this]` lambdas stored beyond lifetime: InfoTab.cpp:45 (RAII Subscription member, fine), Window.cpp:163, Workspace::OpenAsync (Workspace.cpp:229,233) safe only because `m_jobs` is declared last (documented). Dopesheet/AnimCurveView unsubscribe by token in dtor.
- Raw global `Onyx::App::s_activePlayer` (ActiveAnimation.cpp:5): Viewport3D clears it only if it still equals its own player; multiple viewports silently steal it. Not a UAF but surprising.
- `Rendering::GetGlobalContext()` raw pointer read by TexturePool, ImageViewer, VideoPlayer::UploadFrame, Viewport3D. Destructors treat null as "context gone, leak everything", so at app exit every viewer's Vulkan objects are deliberately leaked (VMA leak reports in debug).
- No string_view members; no vector-element-reference-after-push_back found in files read.

## 4. Concurrency inventory
Threads: TaskManager pool (TaskManager.cpp:158, hardware_concurrency, min 2), JobQueue pool (Jobs.cpp:83, Workspace uses 2), VideoPlayer 3 threads + miniaudio callback thread (VideoPlayer.cpp:254-256), GLFW/ImGui main thread. No std::async.
- **TaskManager::getRunningTasks() (TaskManager.cpp:283)** returns a reference to `s_tasks` without lock; createTask (any thread) and collectGarbage mutate it under `s_queueMutex`. `collectGarbage` reads `s_tasks.empty()` (:275) outside the lock; `exit()` iterates it unlocked (:205).
- **TaskManager keeps tasks that threw forever**: collectGarbage keeps `hadException` tasks (:270-272), so `s_tasks.empty()` never becomes true, `runWhenTasksFinished` callbacks never fire, and getRunningTaskCount counts them as running. Only `clearException()` fixes it; nothing in Source calls it.
- **exit() with queued, never-started tasks**: workers `break` on `s_shouldStop` before draining `s_taskQueue` (:175-176); those tasks never get `finish()`, so anything in `TaskHolder::wait()` / `Task::wait()` hangs (atomic_flag wait never notified). `Task::interrupt()` reads `m_interruptCallback` unlocked while `exception()` reads it under m_mutex; `setInterruptCallback` unlocked: data race on std::function.
- `Task` move ctor (:36) does not move `m_background`/`m_exceptionMessage` (never moved in practice).
- `runDeferredCalls` (:306) runs arbitrary callbacks while holding the recursive mutex; `doLater` from workers blocks meanwhile. `runWhenTasksFinished` interrupts every task including background ones as a side effect.
- **JobQueue (Jobs.cpp)**: lanes scanned in `std::map` order, lowest lane id always wins, so a steady stream on low lanes starves higher lanes when workers are saturated. `Cancel()` is a flag only; a cancelled-but-unstarted job still runs its Work.
- **Workspace::SetWorkspaceSettingsPath (Workspace.cpp:275)** reassigns `m_settings` while an in-flight OpenAsync worker holds `ContainerContext::settings` as a reference to it (Workspace.cpp:179). Only guarded by a comment; Settings is not thread-safe.
- EventBus: correct locking. `Subscription::Reset` checks `m_busAlive` then calls Unsubscribe (TOCTOU only if a Subscription dies on another thread concurrently with ~EventBus).
- **VideoPlayer**:
  - `avcodec_flush_buffers(m_videoCtx / m_audioCtx)` called from the demux thread (VideoPlayer.cpp:374-376) while the decode threads are inside send_packet/receive_frame on the same AVCodecContext. FFmpeg codec contexts are not thread-safe: race/crash on seek.
  - SeekByte (UI thread) clears queues, but the demux thread may already hold a pre-seek packet and push it after the clear and before it observes `m_seekReq`: stale packets decoded after the flush.
  - `m_audioClock` is read-modify-written by the audio callback (~600) and stored by the video decode thread (~455): lost update, A/V drift after seek.
  - No drain at EOF (`avcodec_send_packet(NULL)`), last frames never shown.
  - Threads poll with `sleep_for(2-50 ms)` loops instead of condition variables.
- Vulkan from non-render threads: none found. JobQueue decode lane runs CPU-only decoders; `Resources::OneShot` is main-thread only. Good.
- Static locals: Logger `static LogState`/`static Logger` (thread-safe init). EventManager's function-local statics sit in inline header functions: one instance per module if the SDK is ever a DLL. `Window::fullFrame` `static uint32_t crashWatchdog` is main-thread only.

## 5. Globals and singletons
Process-wide; two Window/App instances or isolated tests in one binary share state:
- Singletons: `Logger::Get()` + `Log::state()` (Logger.cpp:50,105), `TypeCatalog::Get()` (TypeCatalog.cpp:5), `TypeRegistry::Get()` (populated by static-init macro ONYX_REGISTER_FILE_TYPE, ITypeHandler.h:71), `AssetVisibility::Get()`, `AppConfig::Get()` (raw pointer set via SetInstance, AppConfig.cpp:46), `Appearance::Get()`, `EventManager` (3 function-local statics), `CameraPanel::s_instance`, `Window::s_windowInstance` (Window.cpp:45), ActiveAnimation global, `ToolkitApi` three raw static pointers (Api/ToolkitApi.cpp:10-12; accessors `std::abort()` if Init was skipped), Metrics globals (Metrics.cpp:13-22), `Threading::g_mainId`.
- Hold Vulkan/ImGui state: FontManager statics (FontManager.cpp:18-24, `ImFont* s_monoFont`, font list, pending-rebuild flag), `Widgets.cpp:285 static unordered_map<ImGuiID,TabCache>`, `ThemeManager.cpp:14 s_flashState`, `Set/GetGlobalContext` (VkContext.cpp:444), `NativeWindow_windows.cpp:23-25` (old WndProc, GLFWwindow*), `NativeMenuBar.cpp:27-29`. ImFont* pointers go stale if the ImGui context is recreated.
- **Layering violation**: `AppConfig::load` (AppConfig.cpp ~170-190) is a static factory that calls `AssetVisibility::Get().ResetAllOverrides()` / `SetUserOverride` and reads `TypeCatalog::Get()`. Loading a config has global side effects. Workspace.h claims "no singleton reach" yet Window.h:150 initialises `m_workspace{TypeCatalog::Get()}` and public `App/TypeVisuals.h` reaches both singletons.
- Init order: all singletons are function-local statics, so construction order is safe. Destruction order is the risk (logging from static destructors; viewers destroyed after EventManager statics). Currently avoided because Window::~Window shuts down explicitly (Window.cpp:215-216 clears EventManager, then TaskManager::exit).
- **TypeCatalog::Register(forcedValue) (TypeCatalog.cpp:16-19)**: `if (v >= m_infos.size()) m_infos.resize(v + 1)`: a legacy forced value like 0x80000000 or a hash allocates gigabytes / throws bad_alloc. When `v < size`, `m_infos[v] = info` silently overwrites whichever type already owned slot v while the old key still maps to v in `m_byKey`. No locking (TypeRegistry has a mutex, TypeCatalog doesn't) although registration happens from AddModule and static init.

## 6. Duplication (pairs)
- Headless vs GUI render: `RenderToImage` (Source/Rendering/RenderToImage.cpp:57-120: CreateScene -> OffscreenTarget -> Build -> OneShot{BeginFrame, RenderBackground, Render, EndFrame} -> Readback) vs `Viewport3D::RenderFrame` (Viewport3D.cpp ~410-440, same sequence + grid/skeleton + PrepareForSampling).
- Camera framing: Cli/Render.cpp:316-351 vs Camera::FocusOn (Rendering/Camera.cpp:116-127) vs Viewport3D::ComputeBounds (Viewport3D.cpp ~277). CLI clamps radius to 1 first, ignores instanceTransform/flipZ that the GUI applies, so CLI and GUI frame the same model differently.
- Bounding-box loops over all vertices: Cli/Render.cpp ~326-333 and Viewport3D.cpp ~294-301.
- Read file/bytes: `IFile::ReadAll` (Vfs/IFile.h:20, zero-padded on short read, no bad_alloc guard) vs Probe.cpp:76-92 vs Goldens.cpp:147 vs RecentFiles.cpp:14 vs Settings/toml::parse_file.
- Lowercasing: Workspace.cpp:130-133, TextEditorViewer.cpp:27-30, SystemTheme_linux.cpp:39, Formatting.h:30-31 (`tolower(char)` is UB for negative chars = any UTF-8 byte).
- Decoder wrappers: DecoderRegistry.cpp:30-100 are three copies of copy-out/try/catch/Diag; Workspace::RunParse repeats identical std::exception/... blocks.
- Viewer toolbars: ImageViewer.cpp ~125-145, TextEditorViewer.cpp:50-97, Viewport3D toolbar each repeat the PushStyleColor(ToolbarButton*) x3 pattern; no shared helper.
- Logging: two macro families (`ONYX_LOG_*` std::format, `ONYX_LOGF_*` printf, ~250 sites). EventManager.h uses the `{}` style, the rest printf.
- Two things named TexturePool: `Rendering::DeferredDestroyQueue` in Include/Onyx/Rendering/TexturePool.h + Source/Rendering/TexturePool.cpp, and the real `App::TexturePool` (Include/Onyx/App/TexturePool.h). Header name collision for consumers.
- `path.string()` narrow conversion in 6+ places (Workspace.cpp:98, Settings.cpp:44, PathUtils::resolvePath, TextEditorViewer ExportAs, OsFile, IsoFileSystem).
- Shutdown guard "if GetGlobalContext()==nullptr leak" reimplemented in TexturePool.cpp ~118, Viewport3D.cpp ~175, ImageViewer dtor.

## 7. Performance smells (the "heavy" part)
1. **`Resources::OneShot` (VkResources.cpp:344-433)** creates a VkCommandPool + VkCommandBuffer + VkFence, submits and blocks in `vkWaitForFences(UINT64_MAX)` on every call. Callers:
   - `Resources::Upload` (VkResources.cpp:190): every vertex buffer, index buffer, material UBO in `SceneRendererVk::Build` = 3 blocking GPU round trips per batch, each with its own CPU_ONLY staging alloc/free. 200 batches = 600+ stalls.
   - `Resources::UploadImage` (:230) per texture incl. mip blit chain.
   - `TexturePool::Update` (App/TexturePool.cpp:58) = once per decoded video frame (staging alloc + pool + fence + wait per frame).
   - `Viewport3D::RenderFrame` (Viewport3D.cpp ~414) = once per redraw per viewport; playing an animation blocks the UI thread on GPU completion every frame; N viewports multiply it. Viewport3D.cpp:97 calls this a "disclosed perf gap".
2. **No VkPipelineCache anywhere** (only `PipelineCache = VK_NULL_HANDLE` for ImGui init, Window.cpp:766; all vkCreateGraphicsPipelines pass VK_NULL_HANDLE: Pipelines.cpp:311/463/605/769). Each `Viewport3D` creates its own Scene+Grid+Background+Overlay pipeline sets (Viewport3D.cpp ~320-330) and `RenderToImage` creates them per call (RenderToImage.cpp ~58-70). N open models = N pipeline-set compiles.
3. **DocumentBrowser::Draw (DocumentBrowser.cpp ~100-209)** redraws the whole tree every frame with recursive TreeNodeEx, no clipping, `doc.path.filename().string()` per document per frame, snprintf per row, two catalog lookups per row. Flat archives with 10^4-10^5 entries cost O(n) per frame.
4. **Logger ring (Logger.cpp:93-98)**: `std::vector<LogEntry>` capped at 4096 and trimmed with `erase(begin(),...)`: once full every log line shifts ~4095 LogEntry (3 strings each) under the global log mutex. `RotatingFileSink` (Logger.cpp:240-244) does `file_size` + open + append + close per line. `Logger::Log` runs `vsnprintf` into a 2 KiB buffer BEFORE the level filter, so disabled Debug logs still pay formatting. LogString copies every sink std::function per call.
5. EventBus::Pump (EventBus.cpp:34-90): per event builds an id vector and re-locks/re-finds per handler (O(handlers^2)). Fine at present rates.
6. Per-frame heap allocs: `RenderSkeleton` builds a fresh `std::vector<OverlayVertex>` each frame (SceneRendererVk.cpp:905); `UploadBatchPalettes` allocates a palette vector per skinned batch per frame (:726); `ComputeJointMatrices` returns two vectors per tick (:706-707); VideoPlayer allocates `fd.pixels` (w*h*4) per frame and `av_packet_alloc` per packet; `TextEditorViewer::Draw` rebuilds and sets a full Palette every frame (~137); Dopesheet `GatherJointIds` builds `std::set<int>` per call.
7. Memory: ImageViewer keeps the CPU `pixels` copy after upload and copies it again on alpha toggle. `AppConfig::save` rebuilds the TOML tree each call; `RecentFiles::Add` re-saves on every add. No by-value SceneData copies found (unique_ptr -> shared_ptr, Viewport3D.cpp ~262).
8. IsoFileSystem recursion opens a new `OsFile` per directory (IsoFileSystem.cpp ~76): exists()+file_size()+ifstream per directory on a DVD image.
9. `std::map` in hot-ish data: `AnimSubstream::samples` (`std::map<int, vector<float>>`, AnimationData.h:49) walked per frame by Dopesheet/AnimCurveView; `SceneNode.h:49 map<TextureRole,int>`. No `std::endl` in hot paths.

## 8. Public header hygiene
- `using namespace` in headers: none (only inside function bodies in Pipelines.cpp).
- **Include/Onyx/Services/PathUtils.h:10** includes `<windows.h>` in a public header with only WIN32_LEAN_AND_MEAN, no NOMINMAX: consumer TUs that include it and use `std::min/max` break (the repo already hit the sibling macro collision, Viewport3D.h:22). Also global `PathUtils` namespace (not `Onyx::`), MAX_PATH truncation silently falls back to `current_path()` (PathUtils.h ~28-31), `resolvePath` returns a narrow string.
- Public headers including `<imgui.h>`: App/TexturePool.h:10, Rendering/AxisGizmo.h:4, Viewers/ImageViewer.h:4, Viewers/VideoPlayer.h:14, Viewers/Viewport3D.h:13. `Rendering/AxisGizmo.h` sits in the render layer that the headless CLI links, so ImGui leaks into that layer.
- VideoPlayer.h exposes `AVPacket*` / FFmpeg / miniaudio types in a public header although ONYX_COMPONENT_MEDIA can be OFF.
- Macros without ONYX_ prefix: `EVENT_DEF`, `EVENT_DEF_NO_LOG`, `EVENT_DEF_IMPL` (EventManager.h:25,61,62), `ASSERT_MAIN_THREAD` (Threading.h:37). Legacy `LOG_*` is opt-in.
- All Include/ headers have `#pragma once`. No non-inline function definitions in headers. EventManager.h function-local statics in inline functions are per-module under shared libs.
- Formatting.h: `FormatBytes` truncates (1.9 KB shows "1 KB"), never shows GB; `MatchesFilter` copies both strings per call (2 allocs per row) and uses signed-char `tolower`.
- "Game-agnostic" SDK still carries game vocabulary: `WadStatsView`, GOW/God of War in UIHelpers.h, GltfExport.h, GameModule.h, MeshData.h, ObjectData.h, SceneNode.h, AssetFormat.h, AssetVisibility.h, Diagnostics.h, Logger.h comments, `SceneData::flipZ` ("GOW2").

## 9. Dead code
- `WadStatsView` (Include/Onyx/App/Panels/WadStatsView.h, Source/App/Panels/WadStatsView.cpp): no references outside itself.
- `Viewport3D::LoadFromMeshData`, ImageViewer `isCompressed` branch + `TextureData::isCompressed/dataSize` (no producer), `IsoFileSystem::ListDirectory` (stub; but part of IVirtualFileSystem and used by Examples/OnyxBox + Tests/mounts_test.cpp).
- `ASSERT_MAIN_THREAD` (never used), `Services::Metrics` (Metrics.cpp; only referenced from Tools/OnyxOracle and UiGallery/Appearance/NativeWindow matches are the word "Metrics" in other senses; no instrumentation call sites in Source/).
- Unused locals/params: Dopesheet.cpp:226 `colBg`, AnimationTimeline.cpp:30 `anim`, SystemFileDialog.cpp:221 `options` (ignored by native dialog), VkContext.cpp:89.
- No `#if 0`. Stale comments: DocumentBrowser.cpp ~170 says double-click decodes "on the UI thread" but ViewerOpening.cpp decodes on a JobQueue lane; Workspace.h:116 "until Shell rework".

## 10. Ranked top-20

| # | Sev | Where | Why / trigger |
|---|-----|-------|---------------|
| 1 | bug | Source/Viewers/VideoPlayer.cpp:372-376 (+341-355) | avcodec_flush_buffers on demux thread while decode threads use the same AVCodecContext; queues cleared racily. Trigger: drag seek slider during playback. Crash/garbage. |
| 2 | bug | Include/Onyx/Viewers/VideoPlayer.h:57 clear(); VideoPlayer.cpp:268-310 | Queue of raw AVPacket* cleared without av_packet_free; Close() never drains. Every seek/scrub/close leaks packets. |
| 3 | bug | VideoPlayer.cpp:297-298 | sws_freeContext without nulling; Close() runs twice (ctor failure then dtor, or Close() then dtor) = double free. |
| 4 | bug | Source/Vfs/IsoFileSystem.cpp:71-124 | Untrusted ISO: uninitialised `uint8_t len`, Read unchecked (EOF -> garbage len, 4 GB rootSize -> near-infinite loop); rec[2..33+nameLen] over-read when len < 34; unaligned reinterpret_cast; self-referencing directory extent recurses to stack overflow. Trigger: truncated/corrupt .iso. |
| 5 | bug | Source/App/Window.cpp:1007-1133 (~1118) | Vulkan results ignored; after vkQueueSubmit2 failure the fence stays reset and the next vkWaitForFences(UINT64_MAX) hangs forever (device lost/driver reset). |
| 6 | bug | Source/App/Window.cpp:894-912 | Release-only catch(...) around the whole frame: no message, EndFrame on unbalanced ImGui stack, can leave fence reset (see 5); Debug differs from Release. |
| 7 | bug | Include/Onyx/Services/EventManager.h:160-174 | Handler unsubscribing during post() erases the multimap node being iterated (UB); a throwing handler aborts delivery to the rest. |
| 8 | bug | Source/Services/Settings.cpp:47-52; AppConfig.cpp:82-86 | Parse error -> empty/default, next Save() overwrites the user's file. One TOML typo = permanent loss. |
| 9 | bug | Source/Services/TaskManager.cpp:267-281, 175-176, 283 | Throwing tasks never collected (runWhenTasksFinished never fires); unstarted tasks at exit() never finish (wait() hangs); getRunningTasks() returns unlocked reference. |
| 10 | bug | Source/Types/TypeCatalog.cpp:16-19 | Register(forcedValue) resizes to forcedValue+1 (huge = OOM) and silently overwrites an existing slot; not thread-safe. |
| 11 | bug | Source/Vfs/OsFile.cpp:6-11; Workspace.cpp:98 | std::string/path.string() = ANSI code page on Windows: non-ASCII/CJK paths fail to open; throwing fs::exists/file_size overloads in a ctor. Same in Settings.cpp:44, PathUtils::resolvePath. |
| 12 | bug | Include/Onyx/Parsers/TextureData.h:20; VkResources.cpp:202-222 | IsValid() only checks !pixels.empty(); Upload memcpys w*h*4. A short pixel buffer = heap over-read. |
| 13 | perf | VkResources.cpp:344-433 (callers :190, :230, App/TexturePool.cpp:58, Viewport3D.cpp ~414) | OneShot = new pool+cmdbuf+fence+blocking wait per call: 3/batch in Build, 1/video frame, 1/3D redraw. Main cause of "heavy". |
| 14 | perf | Pipelines.cpp:311/463/605/769; Viewport3D.cpp ~320; RenderToImage.cpp ~58 | No VkPipelineCache; every Viewport3D and RenderToImage recompiles all pipelines. |
| 15 | perf | Source/App/Panels/DocumentBrowser.cpp ~100-209 | Whole tree redrawn each frame, no clipping, per-frame string allocs; O(n) per frame on large flat archives. |
| 16 | perf | Source/Services/Logger.cpp:93-98, 240-244 | O(4096) vector erase per log line when ring full, under global mutex; file sink open/close per line; printf formatted before level filter. |
| 17 | bug | Source/Modules/Workspace.cpp:179, 275 | Worker holds reference to m_settings while SetWorkspaceSettingsPath reassigns it: data race guarded only by a comment. |
| 18 | maint | Include/Onyx/Services/PathUtils.h:10 | Public header pulls <windows.h> without NOMINMAX; global namespace; MAX_PATH fallback to cwd. |
| 19 | maint | AppConfig.cpp ~170-190, Window.h:150, TypeVisuals.h, FontManager.cpp:18-24, Api/ToolkitApi.cpp:10 | Pervasive process-wide state; AppConfig::load mutates AssetVisibility singleton; two Windows or isolated tests impossible; ToolkitApi abort()s if Init skipped. |
| 20 | maint | Cli/Render.cpp:316-351 vs Camera.cpp:116-127 vs Viewport3D ComputeBounds; RenderToImage.cpp vs Viewport3D::RenderFrame | Duplicated framing/render-pass logic already diverging (CLI ignores instanceTransform/flipZ). Plus dead WadStatsView/Metrics/LoadFromMeshData and game-specific names in a "game-agnostic" SDK. |

Runner-ups: AppConfig::save non-atomic write (AppConfig.cpp:195); RecentFiles throwing fs::exists; IFile::ReadAll zero-padded short reads; Formatting.h MatchesFilter signed-char tolower; DiagSink grows unbounded in the GUI (never drained); JobQueue lane starvation / no cancel-before-start; shutdown leak of onDone closures (Jobs.cpp:108-112); SystemFileDialog ignores `options` (:221); TextEditorViewer::ExportAs unchecked write; AssetReader.h:29 `uint32_t fieldEnd = f.offset + f.size` can wrap; UInt64/Int64/Key stored as `double` in AssetReader (loses bits above 2^53, wrong for 64-bit hashes).
