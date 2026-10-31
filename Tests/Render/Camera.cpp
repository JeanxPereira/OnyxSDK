#include <Onyx/Render/Camera.h>

#include <doctest/doctest.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

using namespace Onyx;

namespace {

Domain::BoundingBox UnitCube() {
    Domain::BoundingBox box;
    box.min = glm::vec3(-0.5f);
    box.max = glm::vec3(0.5f);
    return box;
}

}

TEST_CASE("FocusOn centres the target and backs off past the radius") {
    Render::Camera camera;
    const Domain::BoundingBox box = UnitCube();
    camera.FocusOn(box);
    CHECK(camera.GetTarget() == box.Center());
    CHECK(camera.GetDistance() > box.Radius());
    CHECK(glm::length(camera.GetPosition() - camera.GetTarget()) == doctest::Approx(camera.GetDistance()));
}

TEST_CASE("Proj uses the effective planes and the clip-space flip") {
    Render::Camera camera;
    camera.FocusOn(UnitCube());
    float n = 0.0f;
    float f = 0.0f;
    camera.GetEffectivePlanes(1.5f, n, f);
    CHECK(n > 0.0f);
    CHECK(f > n);
    const glm::mat4 ref = glm::perspective(glm::radians(camera.fov), 1.5f, n, f);
    const Render::Projection p = camera.Proj(1.5f);
    CHECK(p.matrix[0][0] == doctest::Approx(ref[0][0]));
    CHECK(p.matrix[1][1] == doctest::Approx(-ref[1][1]));
}

TEST_CASE("SnapToView tweens to the canonical orientation") {
    Render::Camera camera;
    camera.SnapToView(Render::CameraView::Front);
    CHECK(camera.IsAnimating());
    CHECK(camera.UpdateAnimation(1.0f));
    CHECK_FALSE(camera.IsAnimating());
    CHECK_FALSE(camera.UpdateAnimation(0.1f));
    const glm::vec3 offset = camera.GetPosition() - camera.GetTarget();
    CHECK(offset.x == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(offset.y == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(offset.z == doctest::Approx(camera.GetDistance()));
}

TEST_CASE("Scroll zooms exponentially and clamps") {
    Render::Camera camera;
    const float before = camera.GetDistance();
    camera.ProcessScroll(1.0f);
    CHECK(camera.GetDistance() < before);
    camera.ProcessScroll(-1000.0f);
    CHECK(camera.GetDistance() == doctest::Approx(50000.0f));
}

TEST_CASE("ViewRotation is the rotational part of ViewMatrix") {
    Render::Camera camera;
    const glm::mat4 view = camera.ViewMatrix();
    const glm::mat3 rot = camera.ViewRotation();
    for (int c = 0; c < 3; ++c) {
        for (int r = 0; r < 3; ++r) {
            CHECK(rot[c][r] == doctest::Approx(view[c][r]));
        }
    }
}

TEST_CASE("Camera defaults match the tuning knobs of the original viewer") {
    const Render::Camera camera;
    CHECK(camera.fov == 55.0f);
    CHECK(camera.nearPlane == 0.01f);
    CHECK(camera.farPlane == 50000.0f);
    CHECK(camera.autoNear);
    CHECK(camera.autoFar);
    CHECK(camera.manualNear == 0.1f);
    CHECK(camera.manualFar == 10000.0f);
    CHECK(camera.nearDistanceScale == 0.002f);
    CHECK(camera.farMargin == 1.1f);
    CHECK(camera.nearFarRatioMax == 50000.0f);
    CHECK(camera.GetDistance() == 5.0f);
}

TEST_CASE("GetEffectivePlanes follows the adaptive formula") {
    Render::Camera camera;
    camera.FocusOn(UnitCube());
    const float distance = camera.GetDistance();
    const glm::vec3 position = camera.GetPosition();
    float farDist = 0.0f;
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 corner((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f);
        farDist = std::max(farDist, glm::length(corner - position));
    }
    const float expectedFar = std::max(farDist * 1.1f, distance * 2.0f);
    const float expectedNear = std::max(distance * 0.002f, expectedFar / 50000.0f);

    float n = 0.0f;
    float f = 0.0f;
    camera.GetEffectivePlanes(1.0f, n, f);
    CHECK(f == doctest::Approx(expectedFar));
    CHECK(n == doctest::Approx(std::max(expectedNear, 0.01f)));

    camera.autoNear = false;
    camera.autoFar = false;
    camera.GetEffectivePlanes(1.0f, n, f);
    CHECK(n == 0.1f);
    CHECK(f == 10000.0f);
}
