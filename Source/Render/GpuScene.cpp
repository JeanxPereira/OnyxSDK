#include "Onyx/Render/GpuScene.h"

namespace Onyx::Render {

void GpuScene::SetPalette(std::span<const glm::mat4> palette) {
    const size_t jointCount = m_skeleton ? m_skeleton->joints.size() : 0;
    if (palette.size() != jointCount) {
        m_paletteRejected = true;
        return;
    }
    m_paletteRejected = false;
    m_palette.assign(palette.begin(), palette.end());
}

void GpuScene::SetWorldTransforms(std::span<const glm::mat4> worlds) {
    const size_t jointCount = m_skeleton ? m_skeleton->joints.size() : 0;
    if (worlds.size() != jointCount) {
        m_worldsRejected = true;
        return;
    }
    m_worldsRejected = false;
    m_worlds.assign(worlds.begin(), worlds.end());
}

}
