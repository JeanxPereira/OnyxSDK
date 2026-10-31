#pragma once

#include <Onyx/Core/Domain/Scene.h>

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Onyx::TestKit {

// A synthetic scene plus the camera the render parity gate draws it with. The projection is the
// perspective of fovYDegrees, nearPlane and farPlane at width / height, without any clip-space flip.
struct SceneFixture {
    std::string name;
    Domain::Scene scene;
    glm::mat4 view{1.0f};
    float fovYDegrees = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
    uint32_t width = 512;
    uint32_t height = 512;
    bool textured = false;
};

// sphere-grid, sphere-grid-textured, skinned-cube, blend-stack, joint-chain-200, in this order.
std::vector<SceneFixture> BuildCorpus();

SceneFixture BuildSphereGrid();
SceneFixture BuildSkinnedCube();
SceneFixture BuildBlendStack();
SceneFixture BuildJointChain200();
// The skinned cube plus a one second "bend" clip that swings joints 1 and 2 about X.
SceneFixture BuildAnimatedChain();

// All generators return square RGBA8 textures.
Domain::Texture MakeChecker(uint32_t size, std::array<uint8_t, 4> a, std::array<uint8_t, 4> b, std::string name);
Domain::Texture MakeGradient(uint32_t size, std::array<uint8_t, 4> top, std::array<uint8_t, 4> bottom, std::string name);
Domain::Texture MakeSolid(uint32_t size, std::array<uint8_t, 4> c, std::string name);
Domain::Texture MakeBumpNormal(uint32_t size, std::string name);

}
