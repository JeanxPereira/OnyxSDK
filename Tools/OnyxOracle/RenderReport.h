#pragma once
// ── RenderReport: pure (GL-free) canonical render-report JSON builder ─────
//
// Task 5 renders each corpus scene and calls BuildReport() to produce a
// byte-stable JSON document describing what was drawn: scene name/size, a
// hash of the rendered pixels, and a per-batch geometry/material summary in
// SceneRendererVk::GetBatches() order (Task 11 deleted the GL SceneRenderer
// this comment used to name; RenderBatch/GetBatches() outlived it, now the
// Vulkan renderer's own surface). Byte-stability is the whole point -- the
// report was the equality test between two runs of the GL oracle, and is
// now between two runs of the Vulkan one (OracleReproducible) -- so every
// value is formatted deterministically: fixed-precision floats
// (FormatFloat), manual string building (no ostream locale risk), explicit
// "\n" (never emitted via anything that could turn into "\r\n"), and no
// timestamps, paths, or pointers anywhere in the output.
//
// This header pulls in Rendering::RenderBatch.h for RenderBatch, which only
// forward-declares GLuint (= unsigned int) -- it does NOT include glad, so
// RenderReport.{h,cpp} stay GL-free (there is no GL left to depend on) and
// can be exercised from doctest without any GPU context, same as
// CorpusTextures and CorpusScenes.

#include <Onyx/Rendering/RenderBatch.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Onyx::OracleTool {

// 64-bit FNV-1a (offset basis 14695981039346656037, prime 1099511628211).
uint64_t Fnv1a(const void* data, size_t len);

// Fixed 6-decimal formatting via std::to_chars(..., std::chars_format::fixed,
// 6) -- locale-independent by the standard's specification, unlike
// snprintf("%.6f")/ostream, which read LC_NUMERIC and would render a comma
// decimal separator under e.g. a "de-DE" locale. Negative zero is normalized
// to "0.000000" so -0.0f and 0.0f never diverge in the report.
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

// ── Task 7: masked report comparison ───────────────────────────────────────
//
// The Vulkan oracle's `render-corpus` writes the exact same BuildReport()
// shape as the GL path, from its own renderer-agnostic GetBatches()
// (SceneRendererVk::GetBatches() returns the same Rendering::RenderBatch
// vector type -- see SceneRendererVk.h's top comment). Every field should
// therefore come out byte-identical between the two renderers EXCEPT
// `pixelHash`, which is a hash of the rendered pixel buffer itself and is
// never expected to match across two different rasterizers (different GPU,
// different math, sometimes different pixels within tolerance) -- masking
// that one line is the whole point of this comparison, everything else
// staying byte-exact is what actually pins the report layer.

/// True iff `a` and `b` are identical line-for-line EXCEPT any line that
/// begins with the exact prefix `  "pixelHash": ` (BuildReport's own
/// two-space-indented key), which is skipped on both sides rather than
/// compared -- so the two documents may report different scene names,
/// batch counts, or any other field and still fail here (correctly: that
/// would be a real divergence), but two reports that differ ONLY in their
/// pixelHash value compare equal. Documents with a different number of
/// lines are never equal (compare `--help` in Main.cpp documents this
/// masking rule).
bool JsonEqualMaskingPixelHash(const std::string& a, const std::string& b);

} // namespace Onyx::OracleTool
