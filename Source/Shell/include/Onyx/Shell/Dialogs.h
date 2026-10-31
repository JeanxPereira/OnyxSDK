#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Onyx/Core/Modules/GameModule.h"

namespace Onyx::Shell {

// Native file and folder pickers. Each call blocks until the user answers and returns nullopt when
// the dialog is cancelled or cannot be shown. The Shell calls them between frames, never inside one.
class IDialogs {
public:
    virtual ~IDialogs() = default;

    virtual std::optional<std::filesystem::path> OpenFile(std::span<const Modules::OpenFilter> filters) = 0;
    virtual std::optional<std::filesystem::path> OpenFolder() = 0;
    virtual std::optional<std::filesystem::path> SaveFile(std::string_view defaultName) = 0;
};

// Win32: the common item dialogs. Elsewhere: zenity through popen when it is installed; without it
// every call logs under "shell.dialog" and returns nullopt.
std::unique_ptr<IDialogs> NativeDialogs();

// The star patterns of a filter's extensions: "obx" and ".obx" both give "*.obx"; a pattern that
// already holds a '*' is kept; empty entries are skipped.
std::vector<std::string> FilterGlobs(const Modules::OpenFilter& filter);

}
