#include <Onyx/Render/Projection.h>

#include <doctest/doctest.h>

#include <glm/gtc/matrix_transform.hpp>

using Onyx::Render::Projection;

TEST_CASE("Perspective flips only the Y scale of the glm projection") {
    const float fov = glm::radians(45.0f);
    const glm::mat4 ref = glm::perspective(fov, 1.0f, 0.1f, 100.0f);
    const Projection p = Projection::Perspective(fov, 1.0f, 0.1f, 100.0f);
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (c == 1 && r == 1) {
                CHECK(p.matrix[c][r] == doctest::Approx(-ref[c][r]));
            } else {
                CHECK(p.matrix[c][r] == doctest::Approx(ref[c][r]));
            }
        }
    }
    CHECK(p.matrix[1][1] < 0.0f);
}

TEST_CASE("Orthographic flips only the Y scale of the glm projection") {
    const glm::mat4 ref = glm::ortho(-2.0f, 2.0f, -1.0f, 1.0f, 0.1f, 50.0f);
    const Projection p = Projection::Orthographic(-2.0f, 2.0f, -1.0f, 1.0f, 0.1f, 50.0f);
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (c == 1 && r == 1) {
                CHECK(p.matrix[c][r] == doctest::Approx(-ref[c][r]));
            } else {
                CHECK(p.matrix[c][r] == doctest::Approx(ref[c][r]));
            }
        }
    }
}
