#pragma once

// ── Frame scheduling ──────────────────────────────────────────────────────
// Says whether the app needs to keep drawing.
//
// The window loop sleeps on glfwWaitEventsTimeout when nothing appears to be
// happening, and "appears" used to be inferred entirely from input state: a
// mouse button held, an active item, a key down. Anything driven by *time*
// rather than by input was invisible to that test, so a 0.25s colour
// transition, a progress bar or a playing video got the idle rate (~15 fps)
// and stepped visibly.
//
// This is the missing channel. Anything animating says so, and the loop stops
// guessing:
//
//     Frame::RequestAnimation(0.25);   // I will need frames for this long
//     Frame::RequestRedraw();          // just one more, then you may sleep
//
// Requests are advisory and self-expiring: a client that stops asking (or dies
// mid-animation) costs at most one extra frame, never a permanently spinning
// loop.

namespace Onyx::Frame {

// Draw one more frame after this one, then sleep again unless someone else
// asks. For a state change whose visible effect only lands next frame.
void RequestRedraw();

// Draw continuously for the next `seconds`. Repeated calls extend the deadline
// rather than stacking, so asking for 0.25s every frame keeps it alive without
// unbounded growth.
void RequestAnimation(double seconds);

// Called once at the top of the loop, before the wait/poll decision. Advances
// the clock, consumes a pending single-frame redraw, and returns whether this
// iteration should run at full speed.
//
// Consumption happens here, in the same step that acts on it -- clearing the
// flag anywhere else either eats the request before it draws a frame or leaves
// it set for a second one.
bool BeginFrame(double nowSeconds);

// Pure query, for callers that want to know without consuming (diagnostics).
bool WantsContinuous();

// Seconds until the current animation request expires (0 if none).
double RemainingAnimation();

// Drops every pending request. For shutdown and tests.
void Reset();

} // namespace Onyx::Frame
