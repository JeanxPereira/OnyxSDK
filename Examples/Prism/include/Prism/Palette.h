#pragma once

#include <glm/glm.hpp>

#include <vector>

namespace Prism {

// The colours indexed images decode through. It is the payload of the "prism.palette"
// any-capability and the document's module state.
struct Palette {
    std::vector<glm::u8vec4> colors;
};

}
