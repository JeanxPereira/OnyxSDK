#pragma once
// ── RenderReport: pure (GL-free) canonical render-report JSON builder ─────
//
// Task 5 renders each corpus scene and calls BuildReport() to produce a
// byte-stable JSON document describing what was drawn: scene name/size, a
// hash of the rendered pixels, and a per-batch geometry/material summary in
// Rendering::SceneRenderer::GetBatches() order. Byte-stability is the whole
// point -- the report becomes the equality test between two runs of the GL
// oracle, and later between the GL and Vulkan oracles -- so every value is
// formatted deterministically: fixed-precision floats (FormatFloat), manual
// string building (no ostream locale risk), explicit "\n" (never emitted
// via anything that could turn into "\r\n"), and no timestamps, paths, or
// pointers anywhere in the output.
//
// This header pulls in Rendering::SceneRenderer.h for RenderBatch, which
// only forward-declares GL types (GLuint/GLenum = unsigned int) -- it does
// NOT include glad/GLFW, so RenderReport.{h,cpp} stay GL-free and can be
// exercised from doctest without a GL context, same as CorpusTextures and
// CorpusScenes.

#include <Onyx/Rendering/SceneRenderer.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Onyx::OracleTool {

// 64-bit FNV-1a (offset basis 14695981039346656037, prime 1099511628211).
uint64_t Fnv1a(const void* data, size_t len);

// Fixed 6-decimal formatting ("%.6f"), with negative zero normalized to
// "0.000000" so -0.0f and 0.0f never diverge in the report.
std::string FormatFloat(float v);

// One JSON document describing a rendered scene: scene name, width,
// height, pixelHash, then one object per batch (in `batches` order, i.e.
// SceneRenderer::GetBatches() order) with: name, vertexCount,
// triangleCount, blendMode (Parsers::BlendMode enum name string),
// hasTexture, hasEnvmap, hasSkeleton, metallic, materialColor[4],
// roleTexturesBound, paletteJointCount.
//
// roleTexturesBound counts the nonzero GL ids among a batch's six texture
// role slots (texture0, texture1, texNormal, texAO, texGloss, texScatter).
// That role set will widen when M4 adds more PBR layers; widen the count
// alongside it.
//
// paletteJointCount comes from paletteJointCounts[i] for batch i (0 if
// paletteJointCounts is shorter than batches -- callers are expected to
// pass parallel vectors, but BuildReport never indexes out of bounds).
//
// Two-space indent, keys in the order listed above, LF line endings, no
// trailing whitespace: byte-stable by construction, so identical inputs
// always produce an identical string.
std::string BuildReport(const std::string& sceneName, int w, int h,
                        uint64_t pixelHash,
                        const std::vector<Rendering::RenderBatch>& batches,
                        const std::vector<size_t>& paletteJointCounts);

} // namespace Onyx::OracleTool
