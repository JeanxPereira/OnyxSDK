#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Onyx/Core/Services/Settings.h"

namespace Onyx::Shell {

inline constexpr size_t kMaxRecents = 10;

struct RecentEntry {
    std::filesystem::path path;
    std::string moduleId;
    std::string displayName;
};

// The most recently opened files, newest first, kept in the Settings key "shell.recents" as
// `path|moduleId;...` with `%`, `;` and `|` percent-encoded inside a path. The files are not
// touched when the list loads. The settings object must outlive this one.
class Recents {
public:
    explicit Recents(Services::Settings& settings);

    // Moves an already listed path to the front; the list keeps at most kMaxRecents entries.
    void Add(const std::filesystem::path& path, std::string_view moduleId);
    std::span<const RecentEntry> Entries() const { return m_entries; }
    void Clear();

private:
    void Store();

    Services::Settings& m_settings;
    std::vector<RecentEntry> m_entries;
};

}
