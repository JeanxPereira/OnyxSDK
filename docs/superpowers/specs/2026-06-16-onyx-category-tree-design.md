# Onyx Category Tree View + SCUMM Resource Tree — Design

> Second of three specs (Open → **Treeview** → Informer/Markdown) addressing SCUMMRedux's browsing UX. Today SCUMMRedux's tree shows **rooms only**, and OnyxSDK has no reusable categorized/filtered asset browser — `PakBrowser`/`IsoBrowser` are app-side, flat, and structurally different. This spec adds a **generic `Onyx::App::CategoryTreeView` widget** (groups an `AssetContainer`'s entries by `typeId`, with a text filter + per-category visibility toggles) and has SCUMMRedux **surface all of its resource categories** (Rooms, Scripts, Sounds, Costumes, Charsets, Room Scripts) through it. Mirrors the ScummRev *content-category → viewer* model recovered in the viewer catalog, and adds the filtering ScummRev never had — our differentiator.

## Context & goals

- **App symptom:** `ProfileScumm::ParseContainer` only ever emits `AssetEntry` for rooms (`Source/Gui/ProfileScumm.cpp:20-39`). `ScummIndex` (`Source/ScummCore/Format/IndexParser.h:10-17`) already parses `scripts`, `sounds`, `costumes`, `charsets`, `roomScripts`, but they never reach the UI. `ScummTreePanel` (`Source/Gui/ScummTreePanel.cpp`) renders a flat WAD→entries list with no grouping and no filter.
- **Engine gap (goal #2):** there is no reusable categorized tree-with-filters in Onyx. The generic ingredients exist — `Widgets::ColoredTreeNode` (`Include/Onyx/App/Widgets.h`), `MatchesFilter` (`Include/Onyx/App/Formatting.h:27-33`), `TypeVisuals` icon/color (`Include/Onyx/App/TypeVisuals.h`), and the `TypeCatalog` per-type metadata (`label`/`icon`/`color`/`media`) — but no widget composes them into a grouped, filterable tree. The Explore pass flagged this as a HIGH promotion candidate.
- **Goal:** build that widget **in Onyx** (every consumer gains it; GoWToolkit can adopt it later, additively) and consume it from SCUMMRedux to replace the Rooms-only panel with a full, filterable resource inventory.

## Founding decisions (user-approved)

1. **Generic in Onyx now.** The categorized-tree + filter is an engine widget (triggers an OnyxSDK release), not app-only code. Composes existing engine helpers rather than duplicating them.
2. **Categories at the top.** Level 1 = categories; each expands to its entries. (Not room-centric nesting — that needs resource→room mapping and is deferred.)
3. **Populate all categories now.** SCUMMRedux emits entries for all six resource types immediately; types without a dedicated viewer open as a no-op for now (their rich viewers are later specs). The point of the categorized inventory is to show everything.
4. **Group by `typeId`, not `MediaKind`.** `MediaKind` is too coarse (Costume/Charset have no kind of their own and would collide with Image; Scripts and Room Scripts are both `MediaKind::Script`). Each SCUMM resource type registers its own `typeId` in `TypeCatalog` (label/icon/color), which yields exactly the six distinct categories.
5. **Spec-2 filtering = text + category toggles only.** Substring filter on entry name + show/hide per category. Deep per-type filters (opcode in scripts, limb in costumes, flag in boxes, index in palettes) live *inside* each rich viewer and are deferred to those specs.

## Current state (audited — exact sites)

**OnyxSDK (engine):**
- `Include/Onyx/Domain/Entry.h:71-90` — `AssetEntry{ name, displayName, size, offset, hash, typeId, children (vector, unused today), assetNode, MediaKind kind, ProfileTag profileTag }`.
- `Include/Onyx/Domain/Wad.h:24-30` — `AssetContainer{ filename, fullPath, profile, fileSource, vector<AssetEntry> entries }` (flat list).
- `Source/Ui/PakBrowser.cpp:24-156` — reference pattern: `char m_filter[64]` + `MatchesFilter` + per-entry `ColoredTreeNode` with `IconForType`/`ColorForType`; **no grouping**. `IsoBrowser` similar over VFS. Both are app-side `Source/Ui/` panels (public headers under `Include/Onyx/App/Panels/`).
- `Source/Ui/ViewerRegistry.cpp:26-52` — `Open(entry, wad)`: resolve `entry.typeId` → `ITypeHandler::CreateViewer`; fallback by `MediaKind` factory; else `nullptr`.
- `Include/Onyx/App/IPanel.h:8-14` — `IPanel{ Draw(), getName(), bool visible }`. `Include/Onyx/Api/ToolkitApi.h:23-62` — global facade (`Database()`, `Viewers()`, `Documents()`, `Types()`, `Get/SetSelected()`); panels use the facade, no `AppContext` is passed.
- `TypeCatalog` (`Onyx::Types`) — `Register(TypeInfo{ key, label, media, icon, color })` mints a `TypeId`; used by SCUMMRedux's `RegisterScummTypes`.

**SCUMMRedux (app):**
- `Source/Gui/ProfileScumm.cpp:20-39` — `ParseContainer`: `ParseIndex` → loop `roomDisk` → one `AssetEntry` per room (`typeId=ScummRoomImage`, `kind=Image`, `profileTag=ScummRoomTag{roomId,disk}`).
- `Source/Gui/ProfileScumm.h:7-10` — `ScummRoomTag{ int roomId; uint8_t disk; }`.
- `Source/Gui/ScummTypes.cpp:12-24` — `RegisterScummTypes()`: registers `ScummRoomImage` `TypeInfo` + `ScummRoomImageHandler` via `TypeRegistry::RegisterByTypeId`.
- `Source/ScummCore/Format/IndexParser.h:10-17` — `ScummIndex{ vector<uint8_t> roomDisk; vector<DirEntry> scripts, sounds, costumes, charsets, roomScripts; }`.
- `Source/ScummCore/Format/IndexDir.h:8-11` — `DirEntry{ uint8_t disk; uint32_t offset; }` (`disk==0` ⇒ absent).
- `Source/Gui/ScummTreePanel.cpp` — flat `db.wads → entries` render; click → `SetSelected`; double-click → `Viewers().Open` → `Documents().AddTab`.

## Design

### OnyxSDK (engine)

1. **Pure category model (the testable seam) — no ImGui.** New `Include/Onyx/App/CategoryModel.h` + impl:
   ```cpp
   namespace Onyx::App {
     struct CategoryGroup {
       Onyx::Types::TypeId typeId;
       std::string         label;     // from TypeCatalog
       std::string         icon;      // from TypeCatalog
       float               color[4];  // from TypeCatalog
       std::vector<const Onyx::Domain::AssetEntry*> entries; // filtered, source order preserved
     };
     // Groups entries by typeId; applies a case-insensitive substring filter to displayName/name;
     // drops empty groups; group order = first-appearance of each typeId in `entries`.
     std::vector<CategoryGroup> BuildCategoryModel(
         const std::vector<Onyx::Domain::AssetEntry>& entries,
         std::string_view filter);
   }
   ```
   Metadata (`label`/`icon`/`color`) is looked up from `TypeCatalog` by `typeId`; a `typeId` with no catalog entry falls back to a generic label/icon. This function is what gets unit tests.

2. **`Onyx::App::CategoryTreeView` widget (ImGui shell).** New `Include/Onyx/App/CategoryTreeView.h` + `Source/Ui/CategoryTreeView.cpp`. A stateful, reusable widget (not a closed panel) that a panel's `Draw()` calls:
   ```cpp
   class CategoryTreeView {
   public:
     struct Callbacks {
       std::function<void(const Onyx::Domain::AssetEntry&, Onyx::Domain::AssetContainer&)> onSelect;
       std::function<void(const Onyx::Domain::AssetEntry&, Onyx::Domain::AssetContainer&)> onActivate; // double-click
     };
     void Draw(Onyx::Domain::AssetContainer& container, const Callbacks& cb);
   private:
     char m_filter[128] = {};
     std::unordered_set<uint32_t> m_hiddenTypes; // category toggles (by typeId value)
   };
   ```
   `Draw` renders: a filter `InputTextWithHint` row + a "types" toggle popup (one checkbox per category present, driving `m_hiddenTypes`); then for each `CategoryGroup` from `BuildCategoryModel` (minus hidden types) a `ColoredTreeNode` header showing `icon + label + "(" + count + ")"`, and under it each entry via `ColoredTreeNode` (`IconForType`/`ColorForType`), wired to `onSelect`/`onActivate`. Selection highlight uses the existing `Get/SetSelected` facade.
   - **Config-driven, not molded to one consumer:** the widget knows nothing SCUMM- or GoW-specific. Default group key is `typeId` (covers both apps); no custom group-key hook now (YAGNI).

3. **No changes to `PakBrowser`/`IsoBrowser`.** The new widget is additive. (A future pass may re-skin them onto it; out of scope here.)

### Consumers

- **SCUMMRedux:**
  - **New `typeId`s** in `RegisterScummTypes()` (`Source/Gui/ScummTypes.cpp`): `ScummScript`, `ScummSound`, `ScummCostume`, `ScummCharset`, `ScummRoomScript`, each with a `TypeInfo{label, icon, color, media}` (distinct icon/color so categories read at a glance). No handlers yet for these (their viewers are later specs) ⇒ `Viewers().Open` returns `nullptr` (no-op) for them.
  - **`ProfileScumm::ParseContainer`** also iterates `scripts/sounds/costumes/charsets/roomScripts` (entries with `disk != 0`) and emits an `AssetEntry` per resource (`displayName` e.g. `"Script 12"`), with the matching `typeId`/`kind` and a `profileTag`.
  - **ProfileTag:** generalize to `ScummResourceTag{ ScummResourceKind kind; int id; uint8_t disk; uint32_t offset; }`, carrying what future viewers need (id/disk/offset). The room path migrates to it (`ScummRoomImageHandler` reads `id`/`disk`); the legacy `ScummRoomTag` is removed. *(Plan may instead keep `ScummRoomTag` and add `ScummResourceTag` for the new types if migration churn proves risky — implementer's call, recorded in the plan.)*
  - **`ScummTreePanel`** becomes a thin shell owning a `CategoryTreeView` and forwarding `onActivate` → `Viewers().Open` + `Documents().AddTab`, `onSelect` → `SetSelected`.
- **GoWToolkit:** no change required this spec. The widget is available for a later adoption (would give GoW a grouped/filtered browser); not in scope.

## Data flow

`COMI.LA0 → ParseContainer → ScummIndex → N AssetEntry across 6 typeIds (each with profileTag) → Database` → `ScummTreePanel::Draw` → `CategoryTreeView::Draw(container, cb)` → `BuildCategoryModel(entries, filter)` → groups by typeId (hidden types removed) → ImGui render → double-click → `onActivate` → `Viewers().Open(entry, wad)` (Room → `ImageViewer`; other types → `nullptr` no-op for now). Adding a future viewer = register a handler for that `typeId`; the tree already lists its entries.

## Testing (TDD)

- **OnyxSDK:** unit-test `BuildCategoryModel` headless (no ImGui): groups entries by `typeId`; preserves source order within a group; group order = first appearance; empty filter ⇒ all; substring filter is case-insensitive and drops non-matching entries and now-empty groups; metadata pulled from `TypeCatalog` (and the no-catalog fallback). Existing engine tests + MinimalViewer `--selftest` stay green.
- **SCUMMRedux:** extend `profilescumm_test` to assert per-category entry **counts** vs `ScummIndex` (rooms with `disk!=0`; each DirEntry vector's `disk!=0` count) and that entry[k] has the expected `typeId`/`kind`/`profileTag`. Golden on `minimal_v8.lecf`; real-COMI gated by `SCUMMREDUX_CMI_DIR`.
- **User manual GUI acceptance:** launch `scummredux-gui COMI.LA0` → six categories with counts; expand any; the filter box narrows entries live; the "types" toggle hides/shows categories; double-clicking a Room still opens its background; double-clicking a not-yet-supported type is a harmless no-op.

## Sequencing & release

Stacks on Spec 1 (unreleased). Order:
1. **Spec 1 closes first** — user accepts File→Open, then Task 6 = OnyxSDK **v0.5.0** + consumer bump v0.4.0→v0.5.0.
2. **Then Spec 2** — develop the engine widget + SCUMMRedux consumption against local Onyx via `-DFETCHCONTENT_SOURCE_DIR_ONYXSDK=C:/CodingProjects/Personal/OnyxSDK`; verify all green (OnyxSDK tests, SCUMMRedux `profilescumm_test` + `--selftest`); **stop for the user's manual GUI acceptance of the tree**; then cut OnyxSDK **v0.6.0** and bump consumers' `GIT_TAG` v0.5.0→v0.6.0, rebuild without override.

Method as before: subagent-driven (1 Sonnet implementer + 1 independent Sonnet reviewer), explicit-path commits, no AI trailer, never commit a red tree. In GoWToolkit, never stage the pre-existing dirty paths (`third_party/**`, `tests/fixtures/minimal_v8.lecf`).

## Out of scope (deferred)

- Rich viewers (Room/Costume/Palette/Box/Script) and their in-viewer filters — later specs.
- Spec 3: Informer/Markdown viewer (engine-level).
- Room-centric nesting (resource→room mapping via the chunk tree).
- RNAM-based room names (tree shows `"Room N"`).
- Re-skinning `PakBrowser`/`IsoBrowser` onto `CategoryTreeView`; GoWToolkit adoption.

## Open questions / to verify in the plan

1. **ProfileTag migration vs. addition:** unify on `ScummResourceTag` (migrate room) or keep `ScummRoomTag` + add a sibling tag? Default = unify; fall back to additive if the room-handler change proves risky.
2. **`MediaKind` assignment** for the new types (Costume → Image or Animation? Charset → Image or Raw?) — a coarse hint only (dispatch is by `typeId`); pick sensible values, refine when the viewers land.
3. **Selection model:** the widget uses the global `Get/SetSelected` facade — confirm a single shared selection is fine for SCUMMRedux (it is today) vs. widget-local selection state.
4. **`children` field:** keep entries flat-per-category for now (`AssetEntry.children` stays unused); revisit if room-centric nesting is later adopted.
5. **Category toggle persistence:** `m_hiddenTypes` is in-memory per session (not persisted to config) for Spec 2 — confirm that's acceptable.
