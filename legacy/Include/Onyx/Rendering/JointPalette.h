#pragma once

// ── JointPalette: pure, GL-free joint-skinning math ─────────────────────
//
// Extracted from Source/Rendering/SceneRenderer.cpp (GL, deleted at Task
// 11) so the Vulkan renderer (Source/Rendering/SceneRendererVk.cpp) could
// share the EXACT same rest-pose skinning math instead of carrying its own
// copy — T5 originally ported these three functions verbatim into
// SceneRendererVk.cpp because Onyx_Render could not link Onyx_Render
// (GL); this header became the shared home both renderers built against
// instead, compiled once into each of the (then two) static libraries via
// the same physical source file listed in both CMake source lists. Task 11
// deleted Onyx_Render along with the rest of the GL renderer, so this file
// now compiles exactly once, into Onyx_Render only (CMakeLists.txt's
// ONYX_RENDER_SOURCES) — no more twin-compile to keep in sync.
//
// No GL includes, no Vulkan includes — only glm and Parsers::ObjectData
// (a plain data header). Every function here is pure: no globals, no
// logging, no side effects beyond its return value.
//
// Every function here must stay pure rest-pose/local-TRS math with NO
// clip-space projection step: never call glm::perspective/ortho/frustum/
// project/unProject (or anything that reads GLM_DEPTH_CLIP_SPACE) from
// this file. Before Task 11 that rule guarded against a specific, silent
// failure mode (this source compiling under two different
// GLM_FORCE_DEPTH_ZERO_TO_ONE settings across its two object files, one per
// renderer, with no compile error marking the divergence); the twin-compile
// is gone now, but the rule stays because it is still simply correct for
// what this file is for — rest-pose math has no business touching a
// projection convention at all, on either renderer.

#include <Onyx/Parsers/SceneNode.h> // Parsers::ObjectData

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Onyx::Rendering {

/// Builds the local TRS matrix for joint `i` of `obj`, from
/// Vectors4/5/6 (idle local translation / Q.14 rotation / local scale) —
/// a verbatim port of the reference importer's object-parser local-rotation
/// derivation (see ComputeJointPalette's doc comment for the GLTF-skinning
/// derivation this feeds).
///
/// A per-game bind-pose encoding: one game's object format never sets
/// IsQuaterion, so its skeletons always take the Euler path; another game's
/// format selects the path per joint from a flag bit (`flags & 0x8000`).
///   Euler:      euler_deg = Vectors5 * (1/16384 * 360), ZYX intrinsic
///   Quaternion: quat = Vectors5 * (1/16384), normalized
glm::mat4 BuildLocalTRS(const Onyx::Parsers::ObjectData& obj, int i);

/// Walks `skeleton`'s parent chain (Vectors4/5/6, NOT Matrixes1 — see the
/// note below) computing, per joint:
///   worldRestPose[i] = worldRestPose[parent[i]] * BuildLocalTRS(skeleton, i)
///   palette[i]        = worldRestPose[i] * skeleton.joints[i].bindToJointMat
/// and returns `palette`, index-aligned with skeleton.joints. When
/// `outWorldPos` is non-null it is resized to the same length and filled
/// with each joint's world-space rest-pose origin (worldRestPose[i][3]) —
/// GL's skeleton debug draw (SceneRenderer::RenderSkeleton) needs these
/// separately from the skinning palette itself; Vulkan callers that have
/// no such consumer may pass nullptr.
///
/// Reconstructing the world transform by walking Vectors4/5/6 (rather than
/// reading the file's own Matrixes1) is deliberate: Matrixes1 is
/// parentToJoint in the PS2 VU microcode sense, not the GLTF-equivalent
/// local TRS matrix, and Matrixes3 (bindToJointMat, the inverse bind pose)
/// was computed against the Vectors4/5/6 TRS chain — mixing it with a
/// Matrixes1-derived world pose collapses every skinned vertex.
std::vector<glm::mat4> ComputeJointPalette(const Onyx::Parsers::ObjectData& skeleton,
                                            std::vector<glm::vec3>* outWorldPos = nullptr);

/// Remaps a global-joint-index palette onto one batch's local `jointMap`
/// (local index i -> global index jointMap[i] -> jointPalette[...]).
/// Returns `jointPalette` unchanged (which may itself be empty) when
/// `jointMap` or `jointPalette` is empty — callers that need a guaranteed
/// non-empty result (e.g. a shader palette buffer that must always have at
/// least one entry) apply their own fallback on top of this, exactly as
/// both GL's and Vulkan's call sites already did before this extraction.
std::vector<glm::mat4> BuildBatchPalette(const std::vector<glm::mat4>& jointPalette,
                                          const std::vector<uint16_t>& jointMap);

} // namespace Onyx::Rendering
