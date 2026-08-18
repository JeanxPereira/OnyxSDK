#pragma once
#include "imgui.h"
#include <Onyx/Services/ThemeManager.h>

#include <functional>
#include <string>
#include <vector>

// ── Appearance ────────────────────────────────────────────────────────────
// Single owner of how the UI looks: type, metrics and colour.
//
// The module is split into three layers on purpose, because every scaling and
// font defect this replaces came from mixing them up (see
// docs/design/appearance-system.md):
//
//   State        what the user chose. Serialisable, comparable, persisted.
//   Environment  what the machine measured. Never persisted, never authored.
//   Resolved     everything derived from the two. Never stored.
//
// Resolve() is pure -- no ImGui context, no GL, no globals -- so the whole
// derivation is unit-testable, and the base style can never be polluted by
// initialisation order the way a snapshot of the live style could.
//
// Panels do not touch ImGuiStyle, call ApplyTheme, or rebuild the font atlas.
// They express intent through Mutate(); Commit() applies it, in one place, once
// per frame, outside the ImGui frame (the atlas upload requires that).

namespace Onyx::Appearance {

struct ColorOverride {
    int    imguiCol = 0;
    ImVec4 color    = {0, 0, 0, 1};
};

// ── State: the inputs ─────────────────────────────────────────────────────
struct State {
    // Type
    std::string fontPath;               // "" = the bundled default (see Environment)
    float       fontSizePt = 14.0f;     // logical size, independent of scale/DPI

    // Metrics
    float       userScale  = 1.1f;      // clamped to [kMinScale, kMaxScale]

    // Colour
    ImVec4                     accent = {0.88f, 0.15f, 0.15f, 1.0f};
    Theme::ThemeMode           mode   = Theme::ThemeMode::System;
    std::vector<ColorOverride> overrides;
};

bool operator==(const State& a, const State& b);
inline bool operator!=(const State& a, const State& b) { return !(a == b); }

constexpr float kMinScale    = 0.5f;
constexpr float kMaxScale    = 3.0f;
constexpr float kMinFontSize = 8.0f;
constexpr float kMaxFontSize = 72.0f;

// ── Environment: measured, not chosen ─────────────────────────────────────
struct Environment {
    float nativeScale       = 1.0f;   // monitor content scale (macOS Retina = 2)
    bool  systemPrefersDark = true;   // OS appearance, for ThemeMode::System
    std::string defaultFontPath;      // what an empty State::fontPath resolves to
};

// ── Resolved: derived, never stored ───────────────────────────────────────
struct Resolved {
    float globalScale   = 1.0f;   // userScale * nativeScale
    float textBasePx    = 14.0f;  // -> style.FontSizeBase
    float fontScaleMain = 1.0f;   // -> style.FontScaleMain
    float fontScaleDpi  = 1.0f;   // -> style.FontScaleDpi

    Theme::ThemeMode effectiveMode = Theme::ThemeMode::Dark;  // never System

    ImGuiStyle  style;            // house metrics, scaled, palette applied
    std::string atlasFontPath;    // what the atlas must hold...
    float       atlasRefPx = 14.0f;  // ...and at which reference size
};

// Onyx's house proportions, in logical units (1.0x). Pure: a base that is a
// function rather than a snapshot cannot depend on when it was taken.
ImGuiStyle HouseStyle();

// The entire derivation. Pure -- safe to call from tests with no context.
Resolved Resolve(const State& state, const Environment& env);

// ── Live state ────────────────────────────────────────────────────────────

const State&       Get();
const Environment& Env();

// Edit the desired state. Cheap: only marks it dirty, applies nothing.
// animateColors requests the ease-out palette transition for this change --
// right for a preset button, wrong for a live colour-picker drag.
void Mutate(const std::function<void(State&)>& fn, bool animateColors = false);
void Set(const State& state);

// Update measured values (DPI change, OS theme change, font list ready).
void SetEnvironment(const Environment& env);

// Apply the desired state if it changed. MUST be called outside the ImGui
// frame -- rebuilding the font atlas invalidates the texture the frame's draw
// commands reference. Window drives this once per frame; a call with nothing
// pending costs one comparison.
void Commit();

// The state actually in effect right now (equals Get() after a Commit).
const State& Applied();

// Forces the next Commit() to reapply even if the state compares equal. For
// the rare external change (a consumer poking ImGuiStyle directly).
void Invalidate();

} // namespace Onyx::Appearance
