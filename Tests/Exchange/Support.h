#pragma once

#include <filesystem>
#include <string>
#include <system_error>

#include "TempDir.h"

namespace Onyx::Tests {

struct TempDir {
    Test::TempDir scope;
    std::filesystem::path path;

    explicit TempDir(const std::string& name) : scope("exchange-" + name), path(scope.path()) {}
};

}
