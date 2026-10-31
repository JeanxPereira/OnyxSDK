#include "Onyx/Render/Skinning.h"

#include <format>

namespace Onyx::Render {

namespace {

Services::Diag MakeError(const char* code, std::string message) {
    Services::Diag d;
    d.severity = Services::Severity::Error;
    d.code = code;
    d.message = std::move(message);
    return d;
}

std::string PartLabel(const Domain::MeshPart& part, size_t index) {
    return part.name.empty() ? std::format("part {}", index) : std::format("part '{}'", part.name);
}

}

std::vector<glm::mat4> RestWorldTransforms(const Domain::Skeleton& skeleton) {
    const size_t count = skeleton.joints.size();
    std::vector<glm::mat4> world(count);
    for (size_t j = 0; j < count; ++j) {
        const Domain::Joint& joint = skeleton.joints[j];
        const bool hasParent = joint.parent >= 0 && static_cast<size_t>(joint.parent) < j;
        world[j] = hasParent ? world[static_cast<size_t>(joint.parent)] * joint.localBind : joint.localBind;
    }
    return world;
}

std::vector<glm::mat4> RestPalette(const Domain::Skeleton& skeleton) {
    std::vector<glm::mat4> palette = RestWorldTransforms(skeleton);
    for (size_t j = 0; j < palette.size(); ++j) {
        palette[j] = palette[j] * skeleton.joints[j].inverseBind;
    }
    return palette;
}

std::vector<glm::vec3> JointWorldPositions(const Domain::Skeleton& skeleton,
                                           std::span<const glm::mat4> worldTransforms) {
    const size_t count = std::min(skeleton.joints.size(), worldTransforms.size());
    std::vector<glm::vec3> positions(count);
    for (size_t j = 0; j < count; ++j) {
        positions[j] = glm::vec3(worldTransforms[j][3]);
    }
    return positions;
}

std::vector<Services::Diag> ValidateSkinning(const Domain::Scene& scene) {
    std::vector<Services::Diag> diags;
    const size_t jointCount = scene.skeleton ? scene.skeleton->joints.size() : 0;

    for (size_t p = 0; p < scene.parts.size(); ++p) {
        const Domain::MeshPart& part = scene.parts[p];
        if (part.joints.empty() && part.weights.empty() && part.jointMap.empty()) {
            continue;
        }
        const std::string label = PartLabel(part, p);
        const size_t vertices = part.positions.size();

        const bool streamsMismatch = (!part.joints.empty() && part.joints.size() != vertices) ||
                                     (!part.weights.empty() && part.weights.size() != vertices) ||
                                     part.joints.empty() != part.weights.empty();
        if (streamsMismatch) {
            diags.push_back(MakeError("render.skinning.streams",
                std::format("{}: {} positions, {} joint entries, {} weight entries", label, vertices,
                            part.joints.size(), part.weights.size())));
            continue;
        }

        bool mapOk = true;
        if (!part.joints.empty() && !scene.skeleton) {
            diags.push_back(MakeError("render.skinning.jointMap", std::format("{}: skinned but the scene has no skeleton", label)));
            continue;
        }
        for (uint16_t entry : part.jointMap) {
            if (entry >= jointCount) {
                diags.push_back(MakeError("render.skinning.jointMap",
                    std::format("{}: jointMap entry {} is outside the {} skeleton joints", label, entry, jointCount)));
                mapOk = false;
                break;
            }
        }
        if (!mapOk) {
            continue;
        }

        for (size_t v = 0; v < vertices; ++v) {
            bool bad = false;
            for (int k = 0; k < 4; ++k) {
                if (part.weights[v][k] > 0.0f && part.joints[v][k] >= part.jointMap.size()) {
                    diags.push_back(MakeError("render.skinning.jointIndex",
                        std::format("{}: vertex {} references joint slot {} but jointMap has {} entries", label, v,
                                    part.joints[v][k], part.jointMap.size())));
                    bad = true;
                    break;
                }
            }
            if (bad) {
                break;
            }
        }
    }
    return diags;
}

}
