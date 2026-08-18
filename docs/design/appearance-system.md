# Appearance system — design

Status: **implemented** (2026-08-18), in five commits, one per migration step.
Written after a run of scaling/font defects that all shared one shape.

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

Steps 1-2 already remove the snapshot class of defect; 4 removes the
forgot-to-call-ApplyTheme class.

## What the migration caught

Step 5 turned up a live defect the design predicted but nobody had reported as
such: the font size climbed on its own, 14 -> 15 -> 17 over a session, each step
being the previous value times the 1.10 UI scale, rounded.

`SettingsWindow::Init` baked the atlas outside the owner, which re-seeded
`style.FontSizeBase`. That field is not a setting -- ImGui's
`UpdateCurrentFontSize` keeps it as a live mirror of the current font stack -- so
a panel holding a cached copy of "the font size" could read back the *drawn*
size and hand it in as the *base*. It also showed up as a slider reading 17px
next to a resolved base of 20px.

Both halves of the fix fall out of the design rather than being patched: one
writer (Commit is the only caller of BuildAtlas), and panels rendering
`Get()` instead of caching inputs.

## Colour

Colour followed the same path. `Palette` is a value type and the transition is
`Lerp(from, to, EaseOut(t))` -- pure, so both are tested without a window.
`Commit` is the only writer of `style.Colors`; `Tick` advances the ease-out and
asks `Onyx::Frame` for the frames it needs. Overrides are inputs on `State`,
folded into the target by `Resolve`, so they animate with the rest and persist
like every other input.

Two details that only show up in use:

- `from` is captured from what is **on screen**, not from the last resolved
  palette. Interrupting a transition half way continues from what the user is
  looking at; starting from the resolved palette makes a second click snap back.
- `ApplyTheme` now records intent and the palette lands on the next `Commit`
  rather than inside the call. In the running app that is the same frame's end.

## Frame pacing

An animation is only as good as the frames it gets, and the window loop decided
whether to sleep by inferring activity from input state -- invisible to anything
driven by time. `Onyx::Frame` is the missing channel: `RequestAnimation(seconds)`
and `RequestRedraw()`, consumed once per iteration by `BeginFrame()`. Requests
are deadlines, so re-asking each frame holds one open, and they expire on their
own.

Measured across the same colour transition:

| | idle | during a 0.25s transition |
| --- | --- | --- |
| before | ~82 fps (p50 5.6ms) | ~4 frames |
| after | 13.1 fps | 45 frames (184 fps) |

The idle figure improved because the same commit dropped "any window is a
separate OS viewport" from the activity test: that clause pinned the loop at
full speed forever once a panel was undocked, which is why the stutter appeared
to vanish whenever a floating window was open.
