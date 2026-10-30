#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <string>

namespace Onyx::Domain {

enum class TextureRole : uint8_t { Diffuse, Normal, Occlusion, Gloss, Height, Scatter, Detail, Emissive, EnvMap, Opacity };
enum class BlendMode : uint8_t { Opaque, Alpha, Additive, Subtractive, EnvMap };
enum class NormalEncoding : uint8_t { Xyz, Xy, ReconstructZ };

struct Material {
    std::string name;
    glm::vec4 baseColor{1.0f};
    glm::vec4 tint{1.0f};
    BlendMode blend = BlendMode::Opaque;
    glm::vec2 uvOffset{0.0f};
    float metallic = 0.0f;
    NormalEncoding normalEncoding = NormalEncoding::Xyz;
    std::map<TextureRole, uint32_t> textures;
};

}
