#include "Overlays.h"

#include <Onyx/Render/Skinning.h>

#include <cmath>

namespace Onyx::Render {

namespace {

constexpr float kRootSize = 0.05f;
constexpr float kMarkerSize = 0.02f;
constexpr float kAxisLength = 0.04f;

Shader::OverlayVertex Vertex(const glm::vec3& position, const glm::vec4& color) {
    return {{position.x, position.y, position.z}, {color.r, color.g, color.b, color.a}};
}

glm::vec3 UnitOr(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = glm::length(v);
    return std::isfinite(len) && len > 1e-12f ? v / len : fallback;
}

}

Shader::GridUbo MakeGridUbo(const View& view, const Overlays& overlays) {
    Shader::GridUbo ubo{};
    ubo.viewProj = view.proj.matrix * view.view;
    ubo.invViewProj = glm::inverse(ubo.viewProj);
    ubo.gridColor = overlays.gridColor;
    ubo.cameraPos = glm::vec3(glm::inverse(view.view)[3]);
    ubo.gridScale = overlays.gridScale;
    return ubo;
}

std::vector<Shader::OverlayVertex> BuildSkeletonLines(const Domain::Skeleton& skeleton,
                                                      std::span<const glm::mat4> worlds, const glm::mat4& model,
                                                      const glm::vec4& boneColor) {
    std::vector<Shader::OverlayVertex> lines;
    if (worlds.size() != skeleton.joints.size() || worlds.empty()) {
        return lines;
    }
    const glm::vec4 rootColor(1.0f, 0.3f, 0.1f, 1.0f);
    glm::vec4 markerColor = boneColor * 0.5f + glm::vec4(0.5f);
    markerColor.a = 1.0f;
    const glm::vec4 axisColors[3] = {{1.0f, 0.2f, 0.2f, 1.0f}, {0.2f, 1.0f, 0.2f, 1.0f}, {0.3f, 0.5f, 1.0f, 1.0f}};
    const glm::vec3 axisFallback[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

    const std::vector<glm::vec3> local = JointWorldPositions(skeleton, worlds);
    std::vector<glm::vec3> positions(local.size());
    for (size_t i = 0; i < local.size(); ++i) {
        positions[i] = glm::vec3(model * glm::vec4(local[i], 1.0f));
    }

    lines.reserve(positions.size() * 16);
    for (size_t i = 0; i < positions.size(); ++i) {
        const glm::vec3& pos = positions[i];
        const int32_t parent = skeleton.joints[i].parent;
        if (parent >= 0 && static_cast<size_t>(parent) < positions.size()) {
            lines.push_back(Vertex(positions[static_cast<size_t>(parent)], boneColor));
            lines.push_back(Vertex(pos, boneColor));
        } else {
            for (int axis = 0; axis < 3; ++axis) {
                lines.push_back(Vertex(pos - axisFallback[axis] * kRootSize, rootColor));
                lines.push_back(Vertex(pos + axisFallback[axis] * kRootSize, rootColor));
            }
        }

        for (int axis = 0; axis < 2; ++axis) {
            lines.push_back(Vertex(pos - axisFallback[axis] * kMarkerSize, markerColor));
            lines.push_back(Vertex(pos + axisFallback[axis] * kMarkerSize, markerColor));
        }

        const glm::mat4 oriented = model * worlds[i];
        for (int axis = 0; axis < 3; ++axis) {
            const glm::vec3 direction = UnitOr(glm::vec3(oriented[axis]), axisFallback[axis]);
            lines.push_back(Vertex(pos, axisColors[axis]));
            lines.push_back(Vertex(pos + direction * kAxisLength, axisColors[axis]));
        }
    }
    return lines;
}

}
