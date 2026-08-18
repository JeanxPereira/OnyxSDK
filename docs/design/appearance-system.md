# Appearance system — design

Status: **proposal**, not implemented.
Written 2026-08-18, after a run of scaling/font defects that all shared one shape.

## Why

Seven defects surfaced in one evening. None was a logic error; every one was an
**ownership or ordering** error:

| Defect | What was actually wrong |
| --- | --- |
| UI scale grew the widgets but not the text | Two owners of "how big is text": `ScaleManager` owned the metrics, `FontManager` owned the atlas, and nobody owned `style.FontSizeBase`, which is what ImGui 1.92 actually draws from. |
| Font size slider changed nothing on screen | Same gap from the other side: `BuildAtlas` set the atlas reference size; ImGui seeds `FontSizeBase` from it exactly once (`if (FontSizeBase <= 0)`) and never revisits. |
| Icons drifted as the font size moved | ImGui scales `GlyphOffset` / `GlyphMinAdvanceX` by `drawn ÷ reference`. Two owners disagreeing on those two numbers moved the icons. |
| A scale change reverted the whole look | `ApplyStyleScale` restores from a **snapshot of the live style** taken at init. The snapshot captured whatever happened to be applied at that moment — so the base depended on call order, not on intent. |
| Font size was lost whenever the scale moved | That same snapshot restore clobbered `FontSizeBase`, which is an *input*, not a scaled metric. |
| Bundled SF Mono was never the default | `""` meant both "user has no choice saved" and "the ProggyClean entry's path", so lookup matched the wrong one. |
| Icons sat right of centre in icon buttons | `IconButton` layered on `ImGui::Button`, whose centring contract is the glyph *advance*; an icon needs its *ink* centred. |

The common shape: **state is scattered across modules, derived values are
computed implicitly at call sites, and the order of application lives in
whoever happens to be calling.** Each fix was correct and each left the next
trap in place. That is the thing worth designing away.

## Shape of the fix

One module, `Onyx::Appearance`, with three strictly separated layers.

### 1. State — the inputs

Plain, comparable, serialisable. This *is* what `onyx.toml` stores; there is no
second copy of the truth.

```cpp
namespace Onyx::Appearance {

struct ColorOverride { int imguiCol; ImVec4 color; };

struct State {
    // Type
    std::string fontPath;            // "" = bundled default; resolved, never an index
    float       fontSizePt = 14.0f;  // logical, DPI-independent

    // Metrics
    float       userScale  = 1.1f;   // clamped [0.5, 3.0]

    // Colour
    ImVec4                     accent = {0.88f, 0.15f, 0.15f, 1.0f};
    Theme::ThemeMode           mode   = Theme::ThemeMode::System;
    std::vector<ColorOverride> overrides;

    bool operator==(const State&) const;   // drives dirty detection
};
```

### 2. Environment — measured, not chosen

```cpp
struct Environment {
    float nativeScale = 1.0f;   // monitor content scale
    float dpi         = 96.0f;
};
```

Kept apart from `State` because it is never persisted and never user-authored.
Mixing the two is how "the user's scale" and "the monitor's scale" got
conflated in `GetGlobalScale()`.

### 3. Resolved — derived, never stored

```cpp
struct Resolved {
    float       globalScale;   // userScale * nativeScale
    float       textBasePx;    // -> style.FontSizeBase
    float       fontScaleMain; // -> style.FontScaleMain  (= userScale)
    float       fontScaleDpi;  // -> style.FontScaleDpi   (= nativeScale)
    ImGuiStyle  style;         // fully built: house metrics, scaled, palette applied
    std::string atlasFontPath; // what the atlas must contain
    float       atlasRefPx;    // ...and at which reference size
};

// PURE. No ImGui context, no GL, no globals. This is the whole derivation.
Resolved Resolve(const State&, const Environment&);

// PURE. House proportions in logical units -- the base every Resolve starts from.
ImGuiStyle HouseStyle();
```

`HouseStyle()` replaces the snapshot. A base that is a *pure function* cannot be
polluted by initialisation order, which retires that entire class of defect.

## The one sequenced step

```cpp
void Mutate(std::function<void(State&)> fn);  // panels use this; marks dirty
const State& Get();
void Commit();   // called ONCE per frame by Window, outside the ImGui frame
```

`Commit()` is the only place an order exists:

1. `if (desired == applied) return;`
2. `Resolved r = Resolve(desired, env);`
3. `ImGui::GetStyle() = r.style;` — whole-struct assignment, never incremental
4. if `(r.atlasFontPath, r.atlasRefPx)` differ from what is baked → `BuildAtlas`, set `atlasDirty`
5. `style.FontSizeBase = r.textBasePx; FontScaleMain = r.fontScaleMain; FontScaleDpi = r.fontScaleDpi;`
6. if `atlasDirty` → `UploadAtlas()` — *this* is why `Commit` must run outside the frame
7. `applied = desired;`

Panels never touch `ImGuiStyle`, never call `ApplyTheme`, never call
`BuildAtlas`. They express intent:

```cpp
Appearance::Mutate([&](Appearance::State& s) { s.userScale = v; });
```

Forgetting the follow-up call becomes impossible, because there is no follow-up
call. That alone would have prevented four of the seven defects.

## Properties this buys

- **No snapshots** → the look cannot depend on init order.
- **Idempotent by construction** → the style is assigned, not incrementally
  scaled, so applying twice equals applying once. The old
  `ScaleAllSizes(ratio)` drift cannot recur.
- **Inputs and derived values are type-separated** → nothing can "restore"
  `FontSizeBase` from a snapshot, because it only ever exists in `Resolved`.
- **One writer** → the ordering knowledge lives in one function instead of in
  every slider.

## Cost

The expensive operation is the atlas rebake plus GPU upload. It is gated on
`(fontPath, atlasRefPx)` alone:

- Dragging the **UI scale** never rebakes. ImGui 1.92 rasterises glyphs on
  demand from `FontSizeBase × FontScaleMain × FontScaleDpi`, so scaling is a
  style assignment and a couple of floats.
- Dragging the **font size** rebakes once per distinct size. Widgets may still
  defer with `IsItemDeactivatedAfterEdit`, but that becomes a widget-level
  nicety rather than a correctness requirement — the system is correct either
  way.
- Changing **accent / mode / overrides** only rebuilds the palette.
- A per-frame `Commit()` with no changes is one struct comparison.

## Tests

The point of making `Resolve` and `HouseStyle` pure is that they need no GL
context and go straight into `onyx_tests`:

- **Determinism** — same `State` + `Environment` → byte-identical `ImGuiStyle`,
  regardless of what ran before.
- **Idempotence** — resolving twice equals resolving once.
- **Independence** — mutating `userScale` leaves `fontSizePt`, `accent` and
  `overrides` byte-identical. (The lost-font-size defect.)
- **Scaling law** — metrics scale linearly with `userScale`; `fontScaleMain ==
  userScale`; `textBasePx == fontSizePt`. (The text-does-not-scale defect.)
- **Font resolution** — an empty `fontPath` resolves to the bundled SF Mono,
  never to the ProggyClean entry. (The default-font defect, as a one-liner.)
- **Persistence round-trip** — `State → toml → State` is the identity, so a
  newly added field that nobody persisted fails the suite instead of shipping.

## Widget layer

Same principle, different scope: a wrapper must not inherit a base widget whose
contract differs from what it needs.

- `IconButton` owns its frame and centres on the glyph's ink rect, rather than
  delegating to `ImGui::Button` and inheriting advance-centring. (Done.)
- Introduce `Widgets::Metrics` — small-button inset, icon-button squareness, row
  height — so the house rules live in one place instead of as literals sprinkled
  through each wrapper.

## Migration

Incremental; the tree stays green and consumers keep building at every step.

1. Add `Onyx::Appearance` (`State`, `Environment`, `Resolve`, `HouseStyle`) plus
   the tests above. Nothing wired yet.
2. `Window` calls `Commit()` in `frameEnd`. `Scale::ApplyStyleScale` and
   `Theme::ApplyTheme` keep their signatures and delegate — no consumer change.
3. `AppConfig` stores a `State` instead of loose fields; the TOML keys stay.
4. Settings and the UI Gallery switch to `Mutate`.
5. Delete `ScaleManager`'s snapshot and `Theme::ApplyStyleDefaults` (folded into
   `HouseStyle`), then deprecate the old entry points in one release.

Steps 1–2 already remove the snapshot class of defect; 4 removes the
forgot-to-call-ApplyTheme class.
