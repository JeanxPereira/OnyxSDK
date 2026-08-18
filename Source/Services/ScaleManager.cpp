#include "Services/ScaleManager.h"
#include <Onyx/Services/Appearance.h>
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>

namespace Onyx::Scale {

// ── Static state ──────────────────────────────────────────────────────────
static float s_userScale   = 1.0f;
static float s_nativeScale = 1.0f;


// ── Init ──────────────────────────────────────────────────────────────────

void Init(float userScale, float nativeDpiScale) {
    s_userScale   = std::clamp(userScale, 0.5f, 3.0f);
    s_nativeScale = std::max(nativeDpiScale, 1.0f);

    // No style snapshot any more -- Appearance::HouseStyle() is the base, and a
    // function cannot be captured at the wrong moment.
}

// ── Queries ───────────────────────────────────────────────────────────────

float GetUserScale()   { return s_userScale; }
float GetNativeScale() { return s_nativeScale; }
float GetGlobalScale() { return s_userScale * s_nativeScale; }
float GetFontDpi()     { return s_nativeScale * 96.0f; }

// ── Mutations ─────────────────────────────────────────────────────────────

void SetUserScale(float scale) {
    s_userScale = std::clamp(scale, 0.5f, 3.0f);
    // Appearance is the owner; this stays as the familiar spelling for existing
    // call sites. The change lands on the next Commit().
    Appearance::Mutate([&](Appearance::State& st) { st.userScale = s_userScale; });
}

void SetNativeScale(float scale) {
    s_nativeScale = std::max(scale, 1.0f);
}

// ── Helpers ───────────────────────────────────────────────────────────────

float Scaled(float value) {
    return value * GetGlobalScale();
}

ImVec2 Scaled(const ImVec2& v) {
    float s = GetGlobalScale();
    return ImVec2(v.x * s, v.y * s);
}

ImVec2 Scaled(float x, float y) {
    float s = GetGlobalScale();
    return ImVec2(x * s, y * s);
}

// ── ApplyStyleScale ───────────────────────────────────────────────────────
// Shim. The style is rebuilt from Appearance::HouseStyle() inside
// Appearance::Commit(), so there is no snapshot to restore from any more --
// which is exactly the point: a base captured from the live style depended on
// when it was captured, and a UI-scale change could silently revert the app's
// look to whatever was applied at init.
//
// Kept so existing call sites (and consumers) keep compiling; they should move
// to Appearance::Mutate and this should go.

void ApplyStyleScale(float userScale) {
    const float clamped = std::clamp(userScale, Appearance::kMinScale, Appearance::kMaxScale);
    s_userScale = clamped;
    Appearance::Mutate([clamped](Appearance::State& st) { st.userScale = clamped; });
}

} // namespace Onyx::Scale
