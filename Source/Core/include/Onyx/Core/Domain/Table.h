#pragma once

#include <string>
#include <vector>

namespace Onyx::Domain {

struct Table {
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;
};

}
