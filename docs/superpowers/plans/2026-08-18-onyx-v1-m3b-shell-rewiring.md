# Onyx v1 — M3b: Shell on the Workspace, Legacy Retired

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Shell runs on the Workspace — documents open through modules,
the tree browser and viewers are generic over TypeSpec metadata and decoder
capabilities, events carry ids — and the legacy layer (IAssetProfile,
ProfileManager, AssetDatabase, raw-pointer events, GoW-shaped browsers) is
deleted. Gate: MinimalViewer becomes the OnyxBox GUI proof.

**Architecture:** Contract completion first (Task 1 closes the seams the
M3a final review named), then the Shell is rewired panel by panel with the
legacy path still alive, and only the last coding task deletes it. UI code
is smoke-verified; every decision that CAN be a pure function (viewer
routing, selection payloads, tree filtering) is one, with doctests.

**Tech Stack:** C++20, doctest, ImGui (Shell layer only), CMake/Ninja/MSVC.

**Spec:** `docs/design/2026-08-18-onyx-v1-architecture.md` §7.2/7.4 (events,
progress), §11 (Shell, panels, custom UI), §12 W3.

## Global Constraints

- **Repo:** `E:\CodingProjects\OnyxSDK`, branch `feat/onyx-v1-m3b` (create
  from main at start). Conventional Commits, sentence-case, explicitly
  staged paths, **NO AI attribution** — hard rule.
- **Build:** MSVC dir `E:\CodingProjects\OnyxSDK\build`; never reconfigure
  or delete it; configure failure = STOP and report.
  `cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul && cd /d E:\CodingProjects\OnyxSDK\build && cmake .. && ninja && ctest --output-on-failure'`
- **Layering:** Workspace/Jobs/Cli changes are CORE (no imgui); everything
  else in this plan is SHELL (imgui allowed). LayerGuard enforces Core.
- Suite is 31 ctest entries at branch start; every task ends full-suite
  green plus its own additions.
- Events carry ids/values only (spec §7.4) — a new event carrying a raw
  pointer is a defect, no exceptions.
- MinimalViewer is the in-repo GUI consumer; GoWToolkit is pinned to
  v0.6.x and out of scope.
- **Carried findings from the M3a final review (this plan's mandate):**
  the JobHandle/Progress surface, the pending-drain teardown leak, the CLI
  exit-code inconsistencies, the `--game` hint, `SetWorkspaceSettingsPath`,
  duplicate-name lookup documentation.

---

### Task 1: Contract completion — the seams the Shell needs closed

**Files:**
- Modify: `Source/Services/Jobs.cpp` + `Include/Onyx/Services/Jobs.h`
- Modify: `Include/Onyx/Modules/Workspace.h` + `Source/Modules/Workspace.cpp`
- Modify: `Include/Onyx/Cli/Commands.h` + `Source/Cli/Commands.cpp`
- Tests: extend `Tests/jobs_test.cpp`, `Tests/workspace_test.cpp`,
  `Tests/cli_test.cpp` (update every affected ctest filter)

**Interfaces produced:**
```cpp
// Jobs.h — teardown completeness:
// ~JobQueue() drains m_pending, clearing each queued job's work closure
// (captured resources release; Done callbacks of never-started jobs still
// never run). Document in the dtor comment.

// Workspace.h:
void SetWorkspaceSettingsPath(const std::filesystem::path&); // loads/replaces m_settings
bool CancelOpen(DocumentId);   // cooperative: parseJob.Cancel(); true if a job existed
// Document::parseJob already stored (M3a); expose progress:
// callers use doc->parseJob.Peek() — verify JobHandle::Peek() is public and
// document it on Document.

// Commands.h — exit-code consistency pass:
//   CmdProbe with no winner returns kNoModule (was kOk) — table updated;
//   CmdDecode: decoder-null-result returns kOk when diags explain (salvage),
//   kUsage stays for entry-not-found/no-capability;
//   Run() gains "--game <hint>" passed to Workspace::Open.
```

- [ ] **Step 1: Failing tests** — (a) jobs: submit 3 jobs on one lane into a
  1-worker queue where job 1 blocks; destroy the queue while 2-3 are queued;
  assert via a weak_ptr on a resource captured by job 3's closure that it
  expired after ~JobQueue (the leak test M3a lacked). (b) workspace:
  CancelOpen on a blocked parse → module observing CancelRequested() exits;
  TreeReady{ok=false} delivered; CancelOpen on unknown id → false.
  (c) workspace: SetWorkspaceSettingsPath round-trips a value through a
  temp path. (d) cli: probe-no-winner returns kNoModule; Run with
  `--game obx` opens by hint (add a second never-matching module so the
  hint is load-bearing).
- [ ] **Step 2: Run, expect failures**
- [ ] **Step 3: Implement**
- [ ] **Step 4: Build + full suite + `ctest -R "OnyxJobs|OnyxWorkspace|OnyxCli" --repeat until-fail:3`**
- [ ] **Step 5: Commit** — `feat(modules): Close the Workspace seams the Shell consumes`

### Task 2: App boots a Workspace; modules register through the App

**Files:**
- Modify: `Include/Onyx/App/App.h` + `Source/App/App.cpp`
- Modify: `Source/App/Window.cpp` (construct/own the Workspace)
- Modify: `Examples/MinimalViewer/Source/Main.cpp`
- Test: smoke = MinimalViewer builds and boots (manual step recorded in
  the report with a screenshot description); plus a pure-function doctest
  where any new non-UI helper appears.

**Interfaces produced:**
```cpp
// App.h:
void AddModule(std::unique_ptr<Modules::IGameModule>);  // pre-init only;
                                                        // forwards to the Workspace
Modules::Workspace& GetWorkspace();
```
- Window owns `Modules::Workspace m_workspace{Types::TypeCatalog::Get()};`
  constructed AFTER AppConfig load, BEFORE panels init; destroyed after
  panels (declaration order documented like the M3a member-order comment).
- Legacy AssetDatabase stays alive and untouched this task.
- MinimalViewer registers OnyxBox: `app.AddModule(std::make_unique<OnyxBox::OnyxBoxModule>());`
  (MinimalViewer's CMake links Onyx_ExampleBox.)
- File→Open flow gains the Workspace path behind the existing dialog: if
  `workspace.Probe(path)` has a winner → `OpenAsync`; else fall through to
  the legacy path (both alive until Task 6).

- [ ] Steps: implement → build → run MinimalViewer, open a generated .obx
  (write a helper obx into the repo's build dir via onyxbox-cli or a tiny
  scratch), confirm DocumentOpened/TreeReady arrive (log lines) →
  full suite green → commit `feat(shell): The App boots a Workspace and registers modules`.

### Task 3: The generic document browser

**Files:**
- Create: `Include/Onyx/App/Panels/DocumentBrowser.h` + `Source/App/Panels/DocumentBrowser.cpp` (SHELL)
- Create: `Include/Onyx/Modules/Selection.h` (CORE — the event + path type)
- Modify: `Source/App/App.cpp` (register the panel)
- Test: `Tests/selection_test.cpp` (ctest OnyxSelection) for the pure parts.

**Interfaces produced:**
```cpp
// Selection.h (CORE):
namespace Onyx::Modules {
// Path of child indices from a document root to a node — stable while the
// tree is unchanged, id-safe across frames (no pointers, spec §7.4).
struct NodePath { std::vector<uint32_t> indices; };
struct SelectionChanged { DocumentId doc; NodePath path; };
// Resolve a path against a document's roots; nullptr when stale.
const Domain::AssetEntry* Resolve(const Document&, const NodePath&);
}
```
- Browser: one dock panel listing open documents; per document a tree of
  entries drawn from TypeSpec metadata only (icon/color/label via
  TypeCatalog; Failed nodes get a warning tint + diag count in the
  tooltip). Clicking posts SelectionChanged on the workspace bus.
  Subscribes to DocumentOpened/TreeReady/DocumentClosed (RAII members).
- Pure-function tests: Resolve on the onyxbox fixture tree (valid path,
  stale path after Close → nullptr, out-of-range index → nullptr).

- [ ] Steps: TDD the pure parts → panel implementation → build → manual
  smoke in MinimalViewer (tree shows the .obx entries, Failed entry
  tinted) → suite green → commit `feat(shell): Generic document browser over Workspace trees`.

### Task 4: Viewers routed by decoder capability

**Files:**
- Create: `Include/Onyx/App/ViewerRouting.h` + `Source/App/ViewerRouting.cpp`
  (SHELL, but the decision core is a pure function)
- Modify: `Source/App/App.cpp` / `Source/Viewers/DocumentWindow.cpp`
  (double-click in the browser opens the routed viewer)
- Test: `Tests/viewerrouting_test.cpp` (ctest OnyxViewerRouting)

**Interfaces produced:**
```cpp
// The pure decision (testable without UI):
enum class ViewerKind : uint8_t { None, Image, Text, Scene };
ViewerKind RouteForType(const Modules::DecoderRegistry&, Types::TypeId);
// Priority: Scene > Image > Text (a mesh with a preview image opens the
// viewport). None = no viewer, browser shows "no viewer for <key>".
```
- Shell glue: on SelectionChanged double-click, decode via the registry on
  the MAIN thread for now (decoders are fast for onyxbox; async decode is
  a recorded follow-up), construct ImageViewer/TextEditorViewer/Viewport3D
  with the decoded data through their existing constructors/APIs (read
  them first; adapt minimally).
- Tests: RouteForType against a registry with permutations of capabilities;
  priority asserted; unregistered type → None.

- [ ] Steps: TDD routing → glue → manual smoke (double-click obx image →
  ImageViewer shows the gradient; text → editor shows "hello box") →
  suite green → commit `feat(shell): Viewers open by decoder capability`.

### Task 5: Status and info surfaces on the new events

**Files:**
- Modify: `Source/App/StatusBar.cpp`, `Source/App/InfoTab.cpp`
- Test: pure helpers only (any formatting function added gets a doctest);
  otherwise manual smoke.

- StatusBar: subscribes to DocumentOpened/TreeReady/DocumentClosed on the
  workspace bus; while any document has `!ready`, poll its
  `parseJob.Peek()` each frame and render fraction+label; done → show
  entry count + Error-diag count.
- InfoTab: on SelectionChanged, show the entry's name, full type key
  (KeyOf), size/offset, flags, and the document's diags filtered to that
  entry name (substring match on message — honest note in a comment that
  diag→node association is by name until diags carry a NodePath).
- Legacy event subscriptions in these two files are REMOVED here (the
  legacy events still exist for other panels until Task 6).

- [ ] Steps: implement → manual smoke (open obx: progress flickers, status
  shows counts; select entries: info updates) → suite green → commit
  `feat(shell): Status and info panels follow the Workspace bus`.

### Task 6: Delete the legacy layer

**Files (delete):**
- `Include/Onyx/Domain/IAssetProfile.h`, `Include/Onyx/Services/ProfileManager.h`,
  `Source/Services/ProfileManager.cpp`, `Include/Onyx/Services/AssetDatabase.h`,
  `Source/Services/AssetDatabase.cpp`, `Include/Onyx/App/Panels/IsoBrowser.h`,
  `Source/App/Panels/IsoBrowser.cpp`, `Include/Onyx/App/Panels/PakBrowser.h`,
  `Source/App/Panels/PakBrowser.cpp`
- Legacy EVENT_DEFs in `Include/Onyx/Services/Events.h`: EventWadOpened,
  EventWadClosed, EventPakOpened, EventAllClosed, EventAssetSelected,
  EventAssetLoaded (frame/config/startup events stay — they are Shell
  lifecycle, not asset-model).
**Files (modify):** every remaining consumer the deletion breaks —
`Source/Api/ToolkitApi.*`, `Source/App/App.*`, `Source/App/Window.cpp`,
`Source/App/Panels/{WadStatsView,AnimCurveView,Dopesheet}.cpp`,
`Source/Viewers/{DocumentWindow,Viewport3D}.cpp`, `Include/Onyx/Onyx.h`,
`Tests/profilemanager_test.cpp` (deleted), `Tests/CMakeLists.txt`, root
`CMakeLists.txt` source lists, `Tests/test entries`.
- WadStatsView/AnimCurveView/Dopesheet: where their data source was the
  deleted layer, they degrade to an empty-state panel with a "ports with
  the game modules" note — NOT deleted (their UI is generic value).
- `ProfileTag`/`AssetContainer` fields on AssetEntry/others: remove what
  becomes dead; keep AssetEntry itself (it is the §5.4 node).

- [ ] Steps: delete → chase compile errors file by file (the completeness
  check + LayerGuard keep honesty) → full suite green (profile tests gone,
  count drops — record the new count) → MinimalViewer boots and the OnyxBox
  flow still works → commit `refactor(shell)!: Retire the profile-era loading layer

BREAKING: IAssetProfile, ProfileManager, AssetDatabase and the raw-pointer
asset events are gone; documents open through GameModules and the
Workspace. Consumers pinned to v0.6.x are unaffected until they port.`

### Task 7: Milestone gate

- [ ] Clean-configure scratch build; full suite; repeats on
  `OnyxWorkspace|OnyxCli|OnyxJobs`; LayerGuard green.
- [ ] MinimalViewer GUI proof (the M3 exit exam, run by Jean or recorded
  with his eyes on it): boot → open .obx → tree renders with Failed tint →
  image opens in viewer → text opens in editor → progress visible on a
  slow open (use a large generated box) → close document.
- [ ] CHANGELOG + roadmap (M3 fully done; M0 oracle next, then M4).
  Commit `docs: Record the M3b shell rewiring`.

---

## Self-review notes

- Spec coverage: §7.4 id-only events incl. SelectionChanged ✔T3; §7.2
  progress/cancel surface ✔T1+T5; §11 stock panels generic over
  TypeSpec/decoders ✔T3-T5; W3 deletion list ✔T6 (ITypeHandler survives —
  MinimalViewer/InfoTab still use TypeRegistry handlers? T6 instructs
  chasing consumers; if ITypeHandler ends up consumer-free it MAY be
  deleted in the same task, recorded either way).
- Every M3a-final-review carried finding has a home: pending-drain leak +
  JobHandle surface + settings path + exit codes + --game hint (T1),
  duplicate-name doc (T3 Resolve semantics), mount-aware extract +
  ByteRange widening deferred to the M4/oracle plan where a mounted module
  first exists (recorded in roadmap by T7).
- UI tasks carry manual smoke steps because ImGui panels have no test
  seam; every extracted decision function is doctested. This is the
  honest boundary, stated rather than hidden.
