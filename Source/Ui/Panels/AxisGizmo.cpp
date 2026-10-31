#include "AxisGizmo.h"

#include <algorithm>
#include <array>

namespace Onyx::Ui {

namespace {

struct AxisInfo {
    Render::CameraView view;
    glm::vec3 direction;
    ImU32 color;
    const char* label;
    bool positive;
};

constexpr ImU32 kColorX = IM_COL32(231, 75, 96, 255);
constexpr ImU32 kColorY = IM_COL32(139, 199, 51, 255);
constexpr ImU32 kColorZ = IM_COL32(44, 137, 226, 255);

const std::array<AxisInfo, 6> kAxes = {{
    {Render::CameraView::Right, {1.0f, 0.0f, 0.0f}, kColorX, "X", true},
    {Render::CameraView::Left, {-1.0f, 0.0f, 0.0f}, kColorX, "X", false},
    {Render::CameraView::Top, {0.0f, 1.0f, 0.0f}, kColorY, "Y", true},
    {Render::CameraView::Bottom, {0.0f, -1.0f, 0.0f}, kColorY, "Y", false},
    {Render::CameraView::Front, {0.0f, 0.0f, 1.0f}, kColorZ, "Z", true},
    {Render::CameraView::Back, {0.0f, 0.0f, -1.0f}, kColorZ, "Z", false},
}};

constexpr float kDiscRadius = 10.0f;
constexpr float kHoverGrow = 1.2f;

ImU32 WithAlpha(ImU32 color, unsigned alpha) { return (color & 0x00FFFFFFu) | ((alpha & 0xFFu) << 24); }

}

GizmoHit DrawAxisGizmo(ImDrawList* list, ImVec2 center, float radius, const glm::mat3& viewRotation) {
    struct Cap {
        ImVec2 position;
        float depth;
    };
    std::array<Cap, 6> caps{};
    for (size_t i = 0; i < kAxes.size(); ++i) {
        const glm::vec3 v = viewRotation * kAxes[i].direction;
        caps[i] = {ImVec2(center.x + v.x * radius, center.y - v.y * radius), v.z};
    }

    // Far to near, so a nearer cap paints over a farther one.
    std::array<size_t, 6> order{0, 1, 2, 3, 4, 5};
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return caps[a].depth < caps[b].depth; });

    const ImGuiIO& io = ImGui::GetIO();
    int hovered = -1;
    for (size_t k = order.size(); k-- > 0;) {
        const size_t i = order[k];
        const float dx = io.MousePos.x - caps[i].position.x;
        const float dy = io.MousePos.y - caps[i].position.y;
        if (dx * dx + dy * dy <= kDiscRadius * kDiscRadius * kHoverGrow * kHoverGrow) {
            hovered = static_cast<int>(i);
            break;
        }
    }

    for (const size_t i : order) {
        if (kAxes[i].positive) list->AddLine(center, caps[i].position, kAxes[i].color, 2.0f);
    }
    const ImU32 ink = IM_COL32_WHITE;
    for (const size_t i : order) {
        const AxisInfo& axis = kAxes[i];
        const float r = kDiscRadius * (static_cast<int>(i) == hovered ? kHoverGrow : 1.0f);
        if (axis.positive) {
            list->AddCircleFilled(caps[i].position, r, axis.color, 20);
            list->AddCircle(caps[i].position, r, ImGui::GetColorU32(ImGuiCol_Border), 20, 1.0f);
            const ImVec2 size = ImGui::CalcTextSize(axis.label);
            list->AddText(ImVec2(caps[i].position.x - size.x * 0.5f, caps[i].position.y - size.y * 0.5f), ink,
                          axis.label);
        } else {
            list->AddCircleFilled(caps[i].position, r, WithAlpha(axis.color, 70), 20);
            list->AddCircle(caps[i].position, r, axis.color, 20, 1.5f);
        }
    }

    GizmoHit hit;
    hit.hovered = hovered >= 0;
    if (hovered >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        hit.view = kAxes[static_cast<size_t>(hovered)].view;
    }
    return hit;
}

}
