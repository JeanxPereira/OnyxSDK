#include <Onyx/Render/Camera.h>

#include <doctest/doctest.h>

#include <glm/gtc/matrix_transform.hpp>

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
