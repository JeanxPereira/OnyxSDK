#include <Onyx/Rendering/JointPalette.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>

// See JointPalette.h's own note: every function below must stay clear of
// glm::perspective/ortho/frustum/project/unProject -- pure rest-pose/
// local-TRS math has no business touching a clip-space/NDC-Z convention.
// (Before Task 11 deleted the GL renderer, this file compiled twice --
// once into the GL Onyx_Render, once into the Vulkan renderer (this
// target, renamed Onyx_RenderVk -> Onyx_Render in Task 11's own rename
// commit) PRIVATE-defining GLM_FORCE_DEPTH_ZERO_TO_ONE -- and a
// clip-space call here would have compiled to two silently different
// NDC-Z results; that specific danger is gone with the twin-compile, the
// rule itself stays.)

namespace Onyx::Rendering {

glm::mat4 BuildLocalTRS(const Onyx::Parsers::ObjectData& obj, int i) {
    const auto& v4 = obj.vectors4[i];  // local translation
    const auto& v5 = obj.vectors5[i];  // rotation (Q.14 fixed-point)
    const auto& v6 = obj.vectors6[i];  // local scale

    const float Q14 = 1.0f / (1 << 14);

    // ── Scale ────────────────────────────────────────────────────────────
    glm::mat4 S = glm::scale(glm::mat4(1.0f),
                             glm::vec3(v6.x != 0.0f ? v6.x : 1.0f,
                                       v6.y != 0.0f ? v6.y : 1.0f,
                                       v6.z != 0.0f ? v6.z : 1.0f));

    // ── Rotation ─────────────────────────────────────────────────────────
    glm::quat rot;
    const bool isQuat = obj.joints[i].isQuaternion;

    if (isQuat) {
        // Quaternion Q.14: {x,y,z,w} = v5[0..3] * Q14, then normalize.
        float qx = float(v5.x) * Q14;
        float qy = float(v5.y) * Q14;
        float qz = float(v5.z) * Q14;
        float qw = float(v5.w) * Q14;
        float qlen = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
        if (qlen > 0.0001f) { qx /= qlen; qy /= qlen; qz /= qlen; qw /= qlen; }
        else                 { qx = 0; qy = 0; qz = 0; qw = 1; }
        rot = glm::quat(qw, qx, qy, qz);
    } else {
        // Euler ZYX intrinsic, in degrees (port of utils/math.go EulerToQuat).
        // euler_deg = {v5[0],v5[1],v5[2]} * Q14 * 360
        const float halfToRad = (0.5f * glm::pi<float>()) / 180.0f;
        float ex = float(v5.x) * Q14 * 360.0f * halfToRad;
        float ey = float(v5.y) * Q14 * 360.0f * halfToRad;
        float ez = float(v5.z) * Q14 * 360.0f * halfToRad;
        float sx = std::sin(ex), cx = std::cos(ex);
        float sy = std::sin(ey), cy = std::cos(ey);
        float sz = std::sin(ez), cz = std::cos(ez);
        // ZYX: qx = sx*cy*cz - cx*sy*sz
        //      qy = cx*sy*cz + sx*cy*sz
        //      qz = cx*cy*sz - sx*sy*cz
        //      qw = cx*cy*cz + sx*sy*sz
        float qx = sx * cy * cz - cx * sy * sz;
        float qy = cx * sy * cz + sx * cy * sz;
        float qz = cx * cy * sz - sx * sy * cz;
        float qw = cx * cy * cz + sx * sy * sz;
        float qlen = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
        if (qlen > 0.0001f) { qx /= qlen; qy /= qlen; qz /= qlen; qw /= qlen; }
        else                 { qx = 0; qy = 0; qz = 0; qw = 1; }
        rot = glm::quat(qw, qx, qy, qz);
    }

    glm::mat4 R = glm::mat4_cast(rot);

    // ── Translation ──────────────────────────────────────────────────────
    glm::mat4 T = glm::translate(glm::mat4(1.0f), glm::vec3(v4.x, v4.y, v4.z));

    return T * R * S;
}

std::vector<glm::mat4> ComputeJointPalette(const Onyx::Parsers::ObjectData& skeleton,
                                            std::vector<glm::vec3>* outWorldPos) {
    const size_t n = skeleton.joints.size();
    std::vector<glm::mat4> jointPalette(n, glm::mat4(1.0f));
    if (outWorldPos) outWorldPos->assign(n, glm::vec3(0.0f));

    std::vector<glm::mat4> globalMats(n, glm::mat4(1.0f));
    for (size_t i = 0; i < n; ++i) {
        const auto& j = skeleton.joints[i];
        glm::mat4 local = BuildLocalTRS(skeleton, static_cast<int>(i));
        if (j.parent >= 0 && static_cast<size_t>(j.parent) < n) {
            globalMats[i] = globalMats[static_cast<size_t>(j.parent)] * local;
        } else {
            globalMats[i] = local;
        }
        jointPalette[i] = globalMats[i] * j.bindToJointMat;
        if (outWorldPos) (*outWorldPos)[i] = glm::vec3(globalMats[i][3]);
    }
    return jointPalette;
}

std::vector<glm::mat4> BuildBatchPalette(const std::vector<glm::mat4>& jointPalette,
                                          const std::vector<uint16_t>& jointMap) {
    if (jointMap.empty() || jointPalette.empty()) return jointPalette;

    std::vector<glm::mat4> remapped(jointMap.size(), glm::mat4(1.0f));
    for (size_t i = 0; i < jointMap.size(); ++i) {
        uint16_t globalIdx = jointMap[i];
        if (globalIdx < jointPalette.size()) {
            remapped[i] = jointPalette[globalIdx];
        }
    }
    return remapped;
}

} // namespace Onyx::Rendering
