#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace Onyx::Services {

class Settings;

// Declares the keys a Settings object is expected to hold. For an Enum entry
// the default is an index into `options` and the value is stored as the option
// string.
class SettingsSchema {
public:
    enum class Kind { Bool, Int, Double, String, Enum, Path };

    struct Entry {
        std::string key;
        std::string label;
        Kind kind = Kind::Bool;
        std::variant<bool, int64_t, double, std::string, size_t> def;
        int64_t min = 0;
        int64_t max = 0;
        std::vector<std::string> options;
        double realMin = 0;
        double realMax = 0;
    };

    SettingsSchema& Bool(std::string key, bool def, std::string label = {});
    SettingsSchema& Int(std::string key, int64_t def, int64_t min, int64_t max, std::string label = {});
    SettingsSchema& Double(std::string key, double def, double min, double max, std::string label = {});
    SettingsSchema& String(std::string key, std::string def, std::string label = {});
    SettingsSchema& Enum(std::string key, std::vector<std::string> options, size_t def, std::string label = {});
    SettingsSchema& Path(std::string key, std::string def, std::string label = {});

    const std::vector<Entry>& Entries() const { return m_entries; }

private:
    std::vector<Entry> m_entries;
};

// Sets only the keys the settings object reports absent.
void ApplyDefaults(const SettingsSchema& schema, Settings& settings);

}
