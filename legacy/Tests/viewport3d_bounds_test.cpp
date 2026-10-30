// ── Viewport3D::ComputeBounds tests (T11-review F1) ────────────────────────
//
// F1 found ComputeBounds computing the framing bbox from raw, OBJECT-space
// vertex positions, silently diverging from what SceneRendererVk::Build
// actually draws (which applies scene.instanceTransform, plus a Z-flip
// scale when scene.flipZ, before rasterizing -- see that function's own
// "identical to GL's SceneRenderer::Build" comment). Any asset with a
// non-identity instanceTransform framed on the wrong center; any GOW2
// asset (flipZ=true) framed mirrored in Z.
//
// The seam: Viewport3D::LoadScene() is public, runs ComputeBounds() +
// Camera::FocusOn(m_bounds) BEFORE it ever touches Vulkan
// (EnsureVulkanReady() runs after, and no-ops when there is no live
// global VkContext -- exactly the state a headless doctest binary is in),
// and Camera::GetSceneMin()/GetSceneMax() are public, set verbatim from
// the bbox FocusOn was given. So this is exercised end-to-end through the
// real public API, not a reimplementation of the fix's own math.

#include <doctest/doctest.h>

#include <Onyx/Viewers/Viewport3D.h>
#include <Onyx/Parsers/SceneNode.h>

#include <glm/gtc/matrix_transform.hpp>

using namespace Onyx::Viewers;
using namespace Onyx::Parsers;

namespace {

std::unique_ptr<SceneData> MakeTwoVertexScene(const glm::vec3& translation, bool flipZ) {
    auto scene = std::make_unique<SceneData>();
    scene->instanceTransform = glm::translate(glm::mat4(1.0f), translation);
    scene->flipZ = flipZ;

    MeshPart part;
    part.name = "part0";
    Onyx::Domain::GpuVertex v0;
    v0.position = glm::vec3(0.0f, 0.0f, 0.0f);
    Onyx::Domain::GpuVertex v1;
    v1.position = glm::vec3(1.0f, 2.0f, 3.0f);
    part.vertices = {v0, v1};
    part.indices = {0, 1, 0}; // never read by ComputeBounds; kept non-empty for realism

    scene->meshParts.push_back(part);
    return scene;
}

} // namespace

TEST_CASE("Viewport3D::LoadScene frames bounds in rendered space, not object space (flipZ)") {
    Viewport3D vp("test-viewport-flipz");

    const glm::vec3 translation(10.0f, 0.0f, 0.0f);
    vp.LoadScene(MakeTwoVertexScene(translation, /*flipZ=*/true));

    // Same formula SceneRendererVk::Build applies before rasterizing.
    const glm::mat4 expected =
        glm::scale(glm::translate(glm::mat4(1.0f), translation), glm::vec3(1.0f, 1.0f, -1.0f));
    const glm::vec3 p0 = glm::vec3(expected * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    const glm::vec3 p1 = glm::vec3(expected * glm::vec4(1.0f, 2.0f, 3.0f, 1.0f));
    const glm::vec3 expectedMin = glm::min(p0, p1);
    const glm::vec3 expectedMax = glm::max(p0, p1);

    const glm::vec3 sceneMin = vp.GetCamera().GetSceneMin();
    const glm::vec3 sceneMax = vp.GetCamera().GetSceneMax();

    CHECK(sceneMin.x == doctest::Approx(expectedMin.x));
    CHECK(sceneMin.y == doctest::Approx(expectedMin.y));
    CHECK(sceneMin.z == doctest::Approx(expectedMin.z));
    CHECK(sceneMax.x == doctest::Approx(expectedMax.x));
    CHECK(sceneMax.y == doctest::Approx(expectedMax.y));
    CHECK(sceneMax.z == doctest::Approx(expectedMax.z));

    // Regression guard for the exact bug F1 found: object-space Z (3.0,
    // unflipped) must NOT appear in the framed bounds once flipZ is true.
    CHECK(sceneMax.z != doctest::Approx(3.0f));
    CHECK(sceneMin.z == doctest::Approx(-3.0f));
}

TEST_CASE("Viewport3D::LoadScene frames bounds at the transformed center (no flip)") {
    Viewport3D vp("test-viewport-notranslate");

    const glm::vec3 translation(-5.0f, 2.5f, 1.0f);
    vp.LoadScene(MakeTwoVertexScene(translation, /*flipZ=*/false));

    const glm::mat4 expected = glm::translate(glm::mat4(1.0f), translation);
    const glm::vec3 p0 = glm::vec3(expected * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    const glm::vec3 p1 = glm::vec3(expected * glm::vec4(1.0f, 2.0f, 3.0f, 1.0f));
    const glm::vec3 expectedCenter = (glm::min(p0, p1) + glm::max(p0, p1)) * 0.5f;

    const glm::vec3 sceneCenter =
        (vp.GetCamera().GetSceneMin() + vp.GetCamera().GetSceneMax()) * 0.5f;

    CHECK(sceneCenter.x == doctest::Approx(expectedCenter.x));
    CHECK(sceneCenter.y == doctest::Approx(expectedCenter.y));
    CHECK(sceneCenter.z == doctest::Approx(expectedCenter.z));

    // Regression guard: the object-space center (untranslated) would be
    // (0.5, 1.0, 1.5) -- assert we did NOT frame around that instead.
    CHECK(sceneCenter.x != doctest::Approx(0.5f));
}
