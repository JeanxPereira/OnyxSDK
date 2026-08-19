#pragma once
// ── CorpusScenes: pure (GL-free) synthetic corpus scene builders ──────────
//
// The M0 oracle corpus needs a small set of deterministic, hand-built
// scenes that exercise the render floors (multi-role materials, skinning,
// blend modes, a joint count that exceeds any fixed palette uniform) without
// depending on any real game asset. Each builder below only assembles CPU
// data (Parsers::SceneData) -- no GL calls, no file I/O, no randomness -- so
// it can be exercised directly from doctest as well as from the onyx-oracle
// tool's render-corpus command.

#include <Onyx/Parsers/SceneNode.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Onyx::OracleTool {

// One entry of the corpus: a scene plus the fixed camera it must be
// rendered with. width/height default to the oracle's standard 512x512.
struct CorpusScene {
    std::string           name;      // file stem: "sphere-grid" etc.
    Onyx::Parsers::SceneData scene;
    glm::mat4              view;     // fixed camera, defined per scene
    glm::mat4              proj;     // perspective, fixed fov/aspect/near/far
    int                    width  = 512;
    int                    height = 512;
};

// The whole corpus in canonical order. Deterministic: same output every call.
std::vector<CorpusScene> BuildCorpus();

// Individual builders (BuildCorpus composes these four, in this order):
CorpusScene BuildSphereGrid();    // 3x3 UV-spheres, all 9 TextureRoles bound
CorpusScene BuildSkinnedCube();   // 3-joint chain, rest pose != bind pose
CorpusScene BuildBlendStack();    // checker floor + Normal/Additive/Subtractive quads
CorpusScene BuildJointChain200(); // 200-joint spiral of skinned segments

} // namespace Onyx::OracleTool
