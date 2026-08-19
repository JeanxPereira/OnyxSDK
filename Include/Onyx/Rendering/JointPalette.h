#pragma once

// ── JointPalette: pure, GL-free joint-skinning math ─────────────────────
//
// Extracted from Source/Rendering/SceneRenderer.cpp (GL) so the Vulkan
// renderer (Source/RenderVk/SceneRendererVk.cpp) can share the EXACT same
// rest-pose skinning math instead of carrying its own copy — T5 originally
// ported these three functions verbatim into SceneRendererVk.cpp because
// Onyx_RenderVk cannot link Onyx_Render (see SceneRendererVk.h's
// RenderBatch-reuse comment for why); this header is the shared home both
// GL and Vulkan now build against instead, compiled once into each of the
// Onyx_Render / Onyx_RenderVk static libraries (same source file listed in
// both CMake source lists — see CMakeLists.txt's ONYX_RENDER_SOURCES /
// ONYX_RENDERVK_SOURCES) rather than pulling one library into the other.
//
// No GL includes, no Vulkan includes — only glm and Parsers::ObjectData
// (a plain data header). Every function here is pure: no globals, no
// logging, no side effects beyond its return value.

#include <Onyx/Parsers/SceneNode.h> // Parsers::ObjectData

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Onyx::Rendering {

/// Builds the local TRS matrix for joint `i` of `obj`, from
/// Vectors4/5/6 (idle local translation / Q.14 rotation / local scale) —
/// a verbatim port of the Go reference's obj/export_gltf.go +
/// obj.go GetQuaterionLocalRotationForJoint (see ComputeJointPalette's
/// doc comment for the GLTF-skinning derivation this feeds).
///
/// GOW2 (obj_gow2.go) never sets IsQuaterion, so GOW2 skeletons always
/// take the Euler path; GOW1 (obj.go) sets it from `flags & 0x8000`.
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
