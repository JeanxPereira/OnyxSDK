#pragma once

#include <Onyx/Render/Camera.h>

#include <glm/glm.hpp>
#include <imgui.h>

#include <optional>

namespace Onyx::Ui {

struct GizmoHit {
    // Set on the frame the left button goes down on an axis cap: the view that cap faces.
    std::optional<Render::CameraView> view;
    bool hovered = false;
};

// Draws the six axis caps of the view rotation around `center` into `list` and hit-tests the mouse
// against them, nearest cap first. `viewRotation` maps world directions to view space, so a cap
// whose view-space z is larger faces the viewer more.
GizmoHit DrawAxisGizmo(ImDrawList* list, ImVec2 center, float radius, const glm::mat3& viewRotation);

}
