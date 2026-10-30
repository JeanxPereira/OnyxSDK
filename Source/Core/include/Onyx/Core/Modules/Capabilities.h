#pragma once

#include <string>
#include <vector>

namespace Onyx::Modules {

struct Capabilities {
    bool scene = false;
    bool image = false;
    bool text = false;
    bool audio = false;
    bool table = false;
    bool schema = false;
    std::vector<std::string> any;

    bool Empty() const { return !scene && !image && !text && !audio && !table && !schema && any.empty(); }
};

}
