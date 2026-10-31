#pragma once

#include <OnyxBox/Module.h>
#include <OnyxBox/Writer.h>

#include <doctest/doctest.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "Onyx/Shell/Shell.h"

namespace Onyx::TestSupport {

// A scratch directory under the system temp folder, emptied on construction and removed with the object.
struct TempDir {
    std::filesystem::path path;

    explicit TempDir(const std::string& name) : path(std::filesystem::temp_directory_path() / ("onyx_shell_" + name)) {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        std::filesystem::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

// Writes an .obx holding an 8x8 image "img", a text "txt" and a blob "blob", in that order.
inline std::filesystem::path WriteObx(const std::filesystem::path& file) {
    OnyxBox::Writer writer;
    writer.AddImage("img", 8, 8, std::vector<std::byte>(8 * 8 * 4, std::byte{0x40}));
    writer.AddText("txt", "hello box");
    writer.AddBlob("blob", std::vector<std::byte>(16, std::byte{1}));
    const std::vector<std::byte> bytes = writer.Build();
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return file;
}

inline Shell::ShellOptions TestOptions(const std::filesystem::path& dir, bool hidden = true) {
    Shell::ShellOptions options;
    options.appName = "OnyxShellTest";
    options.configDir = dir / "config";
    options.windowSize = {800, 480};
    options.hiddenWindow = hidden;
    options.gpuValidation = true;
    return options;
}

}
