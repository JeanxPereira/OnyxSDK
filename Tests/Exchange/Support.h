#pragma once

#include <filesystem>
#include <string>
#include <system_error>

namespace Onyx::Tests {

struct TempDir {
    std::filesystem::path path;

    explicit TempDir(const std::string& name)
        : path(std::filesystem::temp_directory_path() / ("onyx_exchange_" + name)) {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        std::filesystem::create_directories(path, ec);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

}
