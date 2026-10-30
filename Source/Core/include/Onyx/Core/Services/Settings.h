#pragma once

#include <Onyx/Core/Result.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Onyx::Services {

// TOML-backed settings with typed access and dirty tracking.
//
// Dotted keys map to tables: only the first dot splits table from key
// ("a.b.c" is table "a", key "b.c"); a key without a dot lands at the root.
// A Get* on a value of another type returns nullopt, never coerces.
//
// Not thread-safe: confine an instance to one thread or guard it externally.
class Settings {
public:
    // A missing file yields an empty, clean object. An unparsable file is
    // copied once to "<stem>.broken.toml" beside it (an existing copy is kept),
    // and the result is empty with Path() still the original and LoadError()
    // set. Save() on such an object refuses until ClearLoadError().
    static Settings Load(const std::filesystem::path& file);
    static Settings Empty();

    // Writes "<path>.tmp" then renames it over the target. Fails with code
    // "settings.broken" while LoadError() is set. Clears Dirty() on success.
    Result<void> Save();

    // Save() only when Dirty(); otherwise succeeds without touching the file.
    Result<void> SaveIfDirty();

    // True if the key holds a value of any type.
    bool Has(std::string_view key) const;

    std::optional<bool> GetBool(std::string_view key) const;
    std::optional<int64_t> GetInt(std::string_view key) const;
    std::optional<double> GetDouble(std::string_view key) const;
    std::optional<std::string> GetString(std::string_view key) const;

    void Set(std::string_view key, bool v);
    void Set(std::string_view key, int64_t v);
    void Set(std::string_view key, double v);
    void Set(std::string_view key, std::string v);
    void Set(std::string_view key, std::string_view v);
    void Set(std::string_view key, const char* v);

    bool Dirty() const;
    const std::filesystem::path& Path() const;
    std::optional<std::string> LoadError() const;
    void ClearLoadError();

    // Independent deep copy: values, path, dirty flag and load error.
    Settings Clone() const;

    ~Settings();
    Settings(Settings&&) noexcept;
    Settings& operator=(Settings&&) noexcept;
    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

private:
    Settings();

    class Impl;
    std::unique_ptr<Impl> impl_;
};

}
