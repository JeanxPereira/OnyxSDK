#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>
#include "TempDir.h"

namespace Onyx::Tests {

// A file under the system temp folder holding `bytes`, removed with the object.
struct BoxFile {
    Test::TempDir scope{"boxfile"};
    std::filesystem::path path;

    BoxFile(const std::string& name, const std::vector<std::byte>& bytes) : path(scope / name) {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    BoxFile(const BoxFile&) = delete;
    BoxFile& operator=(const BoxFile&) = delete;
};

}
