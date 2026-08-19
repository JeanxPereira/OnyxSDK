# Onyx v1 M5 — Generality Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove the SDK is general — a second, unrelated toolkit built with
zero SDK edits — ship `Onyx::TestKit` so every toolkit is born testable,
add glTF export as an external oracle for skinning, and tag **v1.0**.

**Architecture:** The milestone is an exam, not a feature list. No new game
module enters this repo (Jean's scope call, 2026-08-19); the exam instead
audits the SDK's public surface against the two toolkits that already exist
(`MinimalViewer`, `onyxbox-cli`) plus a cold-start toolkit TU compiled from
public headers alone. Every thing a consumer cannot reach is a **finding**,
and the fix is a first-class SDK task, never a workaround. `Onyx::TestKit` is
extracted from the test helpers this repo already grew organically (tree
goldens, decode smoke, render compare) and becomes public surface. glTF
export lands as `Onyx::Exchange` (cgltf), validated by an external tool
(Blender) rather than by our own renderer. The final tasks close the
perimeter debts the M4 review named, freeze the public API per spec §15,
and tag v1.0.

**Tech Stack:** C++20 MSVC, existing Onyx targets, cgltf (FetchContent,
pinned), doctest, ctest, the Vulkan renderer from M4.

**Spec:** `docs/design/2026-08-18-onyx-v1-architecture.md` — §2 (two
floors), §9 (Exchange), §10 (TestKit + oracle strategy), §11 (Shell,
widget library as public contract, the generic CLI's full verb list),
§13 (no game names in Core/Render), §15 (stability policy).
Roadmap: `2026-08-18-onyx-v1-roadmap.md` §M5.

## Global Constraints

- Build dir `build/` is a working MSVC/ninja configure. NEVER reconfigure
  or delete it; a configure failure is STOP-and-report. vcvars64 is the
  BuildTools edition; from Git Bash use `MSYS2_ARG_CONV_EXCL="*"` with
  `cmd /c`. ctest SERIAL only.
- **The exam rule (binding, T2):** a gap in the public surface is a
  FINDING, never something to route around. The audit reports what a
  consumer cannot reach; the controller schedules the SDK fix. No task may
  "solve" a gap by reaching into `Source/**`.
- No game names in Core or Render sources (spec §13). (M4 left three
  pre-existing violations in Render headers — T8 cleans them; do not add
  more.) No new game module enters this repo — Jean's call, 2026-08-19.
- Every new public header compiles standalone.
- The M0 goldens in `Tests/Golden/corpus` stay FROZEN. Render-compare work
  reuses them; nothing regenerates them.
- All new device-requiring ctests skip cleanly (exit 77 + `SKIP_RETURN_CODE
  77`, label `render`).
- Conventional Commits, imperative Sentence-case subject **≤ 72 chars**
  (M4 shipped two over — do not repeat), NO attribution trailers,
  explicit-path staging.
- Commercial game data never enters the repository (spec §10 fixture
  policy). All fixtures are synthetic, written by fixture builders.

## File Structure

```
Include/Onyx/TestKit/{Goldens,DecodeSmoke,RenderCompare}.h   (T1)
Source/TestKit/*.cpp                                          (T1)
docs/design/2026-08-19-public-surface-audit.md              (T2)
Include/Onyx/Exchange/GltfExport.h + Source/Exchange/*.cpp    (T5)
Include/Onyx/Cli/Commands.h + Source/Cli/*.cpp                (T6: render
                                                               moves into
                                                               the SDK)
Include/Onyx/Rendering/RenderToImage.h + Source                (T7: the
                                                               ready floor's
                                                               top step)
Include/Onyx/Onyx.h, Include/Onyx/Rendering/*, README.md      (T8: §13 +
                                                               §15 + umbrella)
docs/, CHANGELOG.md, .github/workflows/ci.yml                 (T9)
```

Order: T1 (TestKit) → T2 (the generality audit) → T5 (glTF) → T6 → T7 →
T8 (perimeter debts; T6 before T7 before T8, see the ledger's scan) → T9
(v1.0). Task numbering keeps its original gaps (T3/T4 were the retired
COMI toolkit/panel tasks) so the ledger and reports stay readable.

---

### Task 1: `Onyx::TestKit` — the SDK ships its own test harness

**Files:** Create `Include/Onyx/TestKit/Goldens.h`, `DecodeSmoke.h`,
`RenderCompare.h` + `Source/TestKit/{Goldens,DecodeSmoke,RenderCompare}.cpp`;
new CMake target `Onyx_TestKit` + alias `Onyx::TestKit` (links Onyx::Core;
RenderCompare additionally links Onyx::Render — judge whether to split the
render half into its own target so a headless-only consumer need not link the
renderer, and state the call); modify `Tests/CMakeLists.txt` to consume it.

**Read first:** the helpers this repo already grew — `Tests/cli_test.cpp`
and `Tests/mounts_test.cpp` (fixture builders, tree walking),
`Tools/OnyxOracle/ImageCompare.{h,cpp}` (the four-knob comparator — THIS is
the render-compare implementation; TestKit exports it, it is not rewritten),
`Tools/OnyxOracle/PngRead/PngWrite`. Extract, do not reinvent; the oracle
keeps working by consuming TestKit (its own copies go away).

**Interfaces (produces):**
```cpp
namespace Onyx::TestKit {
// Tree goldens (spec §10): snapshot names/keys/sizes/payload hashes.
std::string SnapshotTree(const Modules::Document&);        // canonical JSON
bool        CompareTreeGolden(const std::string& snapshot,
                              const std::filesystem::path& goldenFile,
                              std::string& diffOut);        // writes .actual on mismatch
// Decode smoke (spec §10): walk, decode everything decodable.
struct SmokeResult { int decoded, skipped, failed; std::vector<std::string> errors; };
SmokeResult DecodeAll(Modules::Workspace&, Modules::DocumentId,
                      const std::vector<std::string>& allowlist = {});
// Render compare (spec §10) — the M4 comparator, exported.
struct CompareTolerance { int maxChannelDelta; double maxDifferingPct;
                          double maxHighDeltaPct; double maxMae; };
struct CompareResult { bool pass; int maxDelta; double differingPct,
                       highDeltaPct, mae; std::string message; };
CompareResult CompareImages(const std::filesystem::path& a,
                            const std::filesystem::path& b,
                            const CompareTolerance&);
}
```

- [ ] **Step 1: Write the failing tests** in a new `Tests/testkit_test.cpp`
  (ctest entry `OnyxTestKit`, filter matching its TEST_CASE names): tree
  snapshot of a two-entry OBX fixture is canonical (same bytes on repeat,
  entries in tree order, hashes present); `CompareTreeGolden` reports a
  precise diff on a mutated snapshot and writes the `.actual` file;
  `DecodeAll` on the OBX fixture returns decoded=2 failed=0, and on the
  corrupt-entry fixture returns failed≥1 with the entry named, and honors
  an allowlist; `CompareImages` against two of the frozen corpus goldens
  (identical → pass; a golden vs a deliberately perturbed copy → fail with
  the right knob named).
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement by EXTRACTION.** Move `ImageCompare` into TestKit
  verbatim (it is reviewed, tuned and trusted); build the tree-snapshot and
  decode-smoke helpers from the patterns already in `cli_test.cpp`.
- [ ] **Step 4: Repoint the oracle.** `Tools/OnyxOracle` consumes
  `Onyx::TestKit` for comparison instead of its local copy; `VkOracleParity`
  and `OracleReproducible` must stay green and the goldens byte-identical
  (run them 3×). If any parity number moves, STOP — the extraction was not
  faithful.
- [ ] **Step 5: Commit.** `feat(testkit): Ship tree goldens, decode smoke and render compare`

### Task 2: The generality audit — is the SDK actually consumable?

**Scope decision (Jean, 2026-08-19):** the toy COMI module and its toolkit
are OUT. No game module enters this repo. The exam's PURPOSE survives in
this task: prove — or disprove — that a toolkit can be built against
`Include/Onyx/**` alone. The subjects are the two toolkits that already
exist (`Examples/MinimalViewer`, `Examples/OnyxCli`), audited as if they
were third-party consumers.

**Files:** no production code. Create `docs/design/2026-08-19-public-surface-audit.md`.
Any gap this task finds becomes its own SDK task, scheduled by the
controller — never a workaround.

**The audit (do all four, report each as a table):**
1. **Include audit.** For every `#include` in `Examples/**`, classify:
   public (`Include/Onyx/**`), third-party (imgui/glm/volk — legitimate,
   §2 says raw access stays reachable), or **private** (`Source/**`,
   `Tools/**`, or a relative include reaching outside its own example).
   Every private hit is a finding with the exact symbol wanted.
2. **Link audit.** For each example target in CMake: does it link only
   `Onyx::*` aliases plus third-party, or does it compile SDK sources
   directly / link a stub / reach into another target's objects? (M4 left
   `AppConfigStub` history here — verify it is gone.)
3. **Capability audit.** List what each example does (register a module,
   open documents, browse, decode, view, render, run the CLI, add a panel,
   add a viewer, theme, persist layout). For each: is the API that enables
   it declared in a public header AND reachable through `Include/Onyx/Onyx.h`?
   (M4's review found the umbrella header exposes no renderer at all —
   confirm what else is missing from it.)
4. **The cold-start test.** Write the smallest possible toolkit main() —
   register a module, run the app — as a code block IN THE AUDIT DOC (not
   a new target), using only public headers, and verify by compiling it as
   a throwaway TU (`cl /c` against `Include/` + the third-party include
   dirs, exactly as the header-self-sufficiency checks in earlier
   milestones did). If it does not compile, every missing piece is a
   finding.

**Deliverable:** the audit doc with the four tables and a single ranked
"SDK gaps" list at the top: each gap = what a consumer wanted, which
header it should live in, and severity (blocks a toolkit / forces a
workaround / cosmetic). An empty list is the headline result and is what
justifies the v1.0 tag; a non-empty list schedules work before T9.

- [ ] **Step 1:** the four audits.
- [ ] **Step 2:** the cold-start TU compiles (or its failures are findings).
- [ ] **Step 3:** write the doc; commit `docs: Audit the SDK's public surface`.

### Task 5: glTF export — an oracle we did not write

**Files:** root `CMakeLists.txt` (FetchContent cgltf, pinned tag); Create
`Include/Onyx/Exchange/GltfExport.h`, `Source/Exchange/GltfExport.cpp`;
new target `Onyx_Exchange` + alias (links Onyx::Core; NOT the renderer —
export is domain-data-out, spec §9); modify `Source/Cli/Commands.cpp`
(`decode --to gltf`).

**Interfaces:**
```cpp
namespace Onyx::Exchange {
struct GltfOptions { bool embedBuffers = true; bool includeSkin = true; };
bool ExportSceneData(const Parsers::SceneData&, const std::filesystem::path& out,
                     const GltfOptions&, std::string& err);   // .gltf + .bin, or .glb
}
```
Must export: positions/normals/uvs/tangents, materials with baseColor +
the role textures that map cleanly onto glTF PBR (baseColorTexture,
normalTexture, occlusionTexture; document what does NOT map and why),
and — the point of the task — **skins**: joints, inverse bind matrices,
weights, and the rest-pose node hierarchy.

- [ ] **Step 1: Failing tests** (`Tests/gltf_test.cpp`, ctest `OnyxGltf`):
  export the M0 corpus `skinned-cube` `SceneData` (build it via
  `Tools/OnyxOracle/CorpusScenes.h` — link the corpus builders into the
  test as the oracle tests already do); assert the file parses back through
  cgltf with the expected counts (1 mesh, 1 skin, 3 joints, vertex count
  matching), inverse-bind-matrix count == joint count, and every accessor's
  min/max present. Round-trip through cgltf is the machine check.
- [ ] **Step 2: Run, expect FAIL.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: `decode --to gltf`** wired in the CLI for Scene entries,
  with a test through `Cli::Commands` on the OnyxBox mesh fixture.
- [ ] **Step 5: The external oracle (human gate).** Export the skinned cube
  and the 200-joint chain to `build/gltf-check/`. Write
  `docs/gltf-validation.md`: the exact files, what to look for in Blender
  (skin binds to 3 joints; the rest pose matches the corpus PNG; no
  detached vertices; normals outward), and record that this check is
  PENDING a human. Flag it in the report — do NOT claim Blender validation
  you did not perform.
- [ ] **Step 6: Commit.** `feat(exchange): Export SceneData to glTF`

### Task 6: Bring the `render` command into the SDK (M4 debt I3)

**Files:** move the implementation from `Examples/OnyxCli/Render.cpp` into
`Source/Cli/Render.cpp` (compiled into the SDK's CLI sources); modify
`Include/Onyx/Cli/Commands.h` (declare `CmdRender` beside the others),
`Source/Cli/Commands.cpp` (argv dispatch for `render`), `Examples/OnyxCli/
Main.cpp` (drop its local parsing), the CMake for both.

The M4 review's finding: `Include/Onyx/Cli/Render.h` is a public header
whose only definition ships inside an example executable, so a second
toolkit gets a header that will not link — which pre-compromises this
milestone's own exam. Read the header's comment explaining the link cycle
it was avoiding (Cli lives in Core-land; the renderer is a separate target)
and solve it properly: the CLI sources that need the renderer belong in a
target that links it, or `CmdRender` takes an injected render callback that
the composition root supplies. Pick the shape that keeps `Onyx_Core` free
of a renderer dependency, and state the reasoning.
Also add spec §11's missing flags: `--views iso,front,...` (render N
canonical views) and `--strict` (nonzero exit on any Error diag).

- [ ] **Step 1:** Failing test — `render` is reachable through the SDK's
  own CLI entry point (not the example's local parsing), proving a second
  toolkit would get it for free; plus a `--views` test asserting N PNGs and
  a `--strict` exit-code test.
- [ ] **Step 2-3:** Move + wire; keep `onyxbox-cli render` byte-identical
  in behavior (its existing device-gated test must still pass unchanged).
- [ ] **Step 4: Commit.** `refactor(cli): Move the render command into the SDK`

### Task 7: `RenderToImage` — the ready floor's top step (M4 debt I5)

**Files:** Create `Include/Onyx/Rendering/RenderToImage.h` +
`Source/Rendering/RenderToImage.cpp`; refactor the three in-tree consumers
that re-implement the same ~60 lines (`Source/Cli/Render.cpp` post-T6,
`Source/Viewers/Viewport3D.cpp`, `Tools/OnyxOracle/Main.cpp`).

```cpp
namespace Onyx::Rendering {
struct RenderRequest { const Parsers::SceneData& scene; int width, height;
                       glm::mat4 view, proj; ShadingMode mode; };
// Owns context+pipelines+target lifetime; returns tightly packed top-down RGBA.
bool RenderToImage(const RenderRequest&, std::vector<uint8_t>& rgbaOut,
                   std::string& err);
// Overload reusing a caller-owned VkContext for repeated renders.
bool RenderToImage(VkContext&, const RenderRequest&,
                   std::vector<uint8_t>& rgbaOut, std::string& err);
}
```
Spec §2's promise is that the ready floor "covers the common case in a few
lines"; today it takes a `VkContext&`, a `VkCommandBuffer` and a
caller-created pipeline object. This closes that.
**Gate:** after refactoring the oracle onto it, `VkOracleParity` and
`OracleReproducible` stay green and the goldens byte-identical (3× runs).
If a pixel moves, STOP and report BLOCKED.

- [ ] **Step 1:** Failing test: `RenderToImage` on the corpus blend-stack
  produces a non-uniform image, twice byte-identical, with no caller-side
  Vulkan types touched (the test itself must not include volk).
- [ ] **Step 2-4:** Implement; refactor the three consumers; run the gates.
- [ ] **Step 5: Commit.** `feat(render): Add a one-call headless render entry point`

### Task 8: Public-surface hygiene — §13, §15, and the umbrella header

**Files:** `Include/Onyx/Onyx.h`, `Include/Onyx/Rendering/RenderBatch.h`,
`Include/Onyx/Rendering/JointPalette.h`, `Include/Onyx/Parsers/TextureData.h`,
`Source/RenderVk/SceneRendererVk.cpp`, `README.md`, plus the RenderVk→
Rendering path fold.

1. **§13 violations the M4 review named:** `RenderBatch.h:62,79` and
   `JointPalette.h:47` carry game names (`GOWR`, `GOW2`, `_0n_`/`_0ao_`
   suffix conventions) in Render-layer PUBLIC headers; `SceneRendererVk.cpp:69`
   too. Rewrite those comments in game-neutral terms (describe the
   mechanism, not the game). `TextureData.h:16`'s `glInternalFormat`
   (GL vocabulary, ZERO readers tree-wide) — remove it or rename to a
   neutral concept; removing is preferred if nothing reads it (verify).
2. **`RenderBatch.h:30`'s global-namespace `using GLuint = unsigned int;`**
   in a public header pollutes every consumer TU — move it inside the
   namespace or replace with `uint32_t` (verify no ABI/behavior change).
3. **The three-names problem:** one layer answers to `Onyx::Rendering`
   (namespace), `Onyx::Render` (target alias) and `Onyx/RenderVk/` (include
   path). Fold the include path: move `Include/Onyx/RenderVk/*` →
   `Include/Onyx/Rendering/`, `Source/RenderVk/*` → `Source/Rendering/`,
   update includes and CMake lists. Mechanical, its own commit.
4. **Umbrella header:** `Include/Onyx/Onyx.h` includes nothing from the
   renderer — the milestone's whole deliverable is invisible to
   `#include <Onyx/Onyx.h>`. Add the renderer's public entry points
   (SceneRenderer, RenderToImage, RenderContext) and TestKit/Exchange.
5. **§15 stability policy in the README:** pre/post-1.0 rules, public
   surface = `Include/Onyx/**` except `Detail/**`, API-not-ABI. Add the
   `Include/Onyx/Detail/` convention if any header should live there.

- [ ] Each item its own commit where it is mechanical; suite green after
  each; final: `docs: State the stability policy` + `refactor(render): Fold
  the RenderVk include path into Rendering`.

### Task 9: v1.0 — CHANGELOG, roadmap, CI, tag

**Files:** `CHANGELOG.md`, `docs/superpowers/plans/2026-08-18-onyx-v1-roadmap.md`,
`.github/workflows/ci.yml`, git tag.

- [ ] **Step 1:** CHANGELOG: close `## Unreleased` into `## v1.0.0 —
  2026-08-XX` with the milestone story (M1-M5), carrying the Known-gaps
  section forward (it is the v1 promise). Roadmap: M5 marked DONE, the
  whole v1 marked complete, post-v1 backlog listed (GoWToolkit port, the
  M4 deferrals that remain, animation playback on the Vulkan renderer,
  the metrics ratchet).
- [ ] **Step 2:** CI: add the TestKit tests to both jobs; add a step that
  compiles T2's cold-start toolkit TU against public headers (the exam must
  not rot); keep the lavapipe leg honest (still never-run until the first
  push).
- [ ] **Step 3: The gate itself** — run every milestone gate one last time:
  full serial ctest, parity + reproducibility 3×, YAML lint, both toolkits'
  timeout-runs, `git diff --stat -- Tests/Golden` empty.
- [ ] **Step 4: Tag.** Annotated `v1.0.0` per the repo's release convention
  (`git tag -a v1.0.0 -m "v1.0.0 — ..."`). Do NOT push — pushing is a
  human action this project has not authorized.
- [ ] **Step 5: Commit.** `docs: Release v1.0.0`

---

## Milestone Gate (roadmap M5, restated)

1. **The exam (T2):** the public-surface audit's gaps list is either empty
   or every entry has a scheduled fix that landed. A non-empty list with no
   fixes means the SDK is not yet general and v1.0 is premature. The
   cold-start toolkit TU compiles against public headers alone.
2. TestKit green in CI and consumed by the oracle (parity unchanged).
3. glTF export machine-verified via cgltf round-trip; Blender validation
   documented and flagged PENDING a human.
4. The M4 debts closed: `render` in the SDK (T6), `RenderToImage` (T7),
   §13/§15/umbrella hygiene (T8).
5. Full serial suite green; goldens byte-identical; v1.0.0 tagged locally.

## Self-review notes

- The exam rule is the milestone's spine: T2-T4 are worth little if an
  implementer "helpfully" edits the SDK to make the example compile. Every
  dispatch for those tasks must restate it.
- Two human gates are named and must not be silently satisfied: Blender
  validation of the glTF skin (T5) and the push that finally exercises the
  lavapipe CI leg. Both are recorded as PENDING, not skipped.
- Interfaces cross-checked at plan time: `IGameModule`/`TypeRegistrar`/
  `DecoderRegistry`/`ContainerContext` (M3), `ByteRange` (M0),
  `Cli::Commands` exit codes, `IPanel`/`Widgets.h`/`PanelRegistry`,
  `SceneData`/`MaterialDesc`/`TextureRole`, `ImageCompare`'s four knobs (M4).
- cgltf is pinned by tag at T5's dispatch; the implementer verifies the tag
  exists and discloses any substitution (the M4 volk precedent).
