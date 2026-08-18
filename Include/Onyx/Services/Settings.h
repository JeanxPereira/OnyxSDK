#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Onyx::Services {

// Scoped TOML settings service with typed access and dirty tracking.
//
// Scope convention (wired by M3's Workspace):
//   - App scope: onyx-settings.toml beside onyx.toml
//   - Workspace scope: .onyx/workspace.toml under the opened folder
//
// Key format:
//   - Dotted keys ("gowr.texIndexDir") map to [gowr] section, texIndexDir = ...
//   - Only the first dot splits table from key ("a.b.c" → table "a", key "b.c")
//   - Keys without a dot land at document root
//
// Load(path) returns an empty Settings if the file does not exist.
// Set() marks the instance Dirty(); Save() clears it.
//
// Not thread-safe: confine an instance to one thread or guard it
// externally.
class Settings {
 public:
    // Load from file. Missing file yields an empty, clean settings object.
    static Settings Load(const std::filesystem::path& file);

    // Save to the path passed to Load(). Returns false on write error.
    // Clears Dirty() on success.
    bool Save();

    // Typed access — type mismatch returns std::nullopt.
    std::optional<bool> GetBool(std::string_view key) const;
    std::optional<int64_t> GetInt(std::string_view key) const;
    std::optional<double> GetDouble(std::string_view key) const;
    std::optional<std::string> GetString(std::string_view key) const;

    // Set a value — marks Dirty().
    void Set(std::string_view key, bool v);
    void Set(std::string_view key, int64_t v);
    void Set(std::string_view key, double v);
    void Set(std::string_view key, std::string v);

    // Query state.
    bool Dirty() const;
    const std::filesystem::path& Path() const;

    ~Settings();

    // Move-only type (unique_ptr requires move semantics).
    Settings(Settings&&) = default;
    Settings& operator=(Settings&&) = default;
    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

 private:
    Settings();

    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace Onyx::Services
