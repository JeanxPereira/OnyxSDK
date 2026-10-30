#include <Onyx/Core/Services/SettingsSchema.h>

#include <Onyx/Core/Services/Settings.h>

#include <utility>

namespace Onyx::Services {

SettingsSchema& SettingsSchema::Bool(std::string key, bool def, std::string label) {
    m_entries.push_back({std::move(key), std::move(label), Kind::Bool, def, 0, 0, {}});
    return *this;
}

SettingsSchema& SettingsSchema::Int(std::string key, int64_t def, int64_t min, int64_t max, std::string label) {
    m_entries.push_back({std::move(key), std::move(label), Kind::Int, def, min, max, {}});
    return *this;
}

SettingsSchema& SettingsSchema::Double(std::string key, double def, double min, double max, std::string label) {
    m_entries.push_back({std::move(key), std::move(label), Kind::Double, def, 0, 0, {}, min, max});
    return *this;
}

SettingsSchema& SettingsSchema::String(std::string key, std::string def, std::string label) {
    m_entries.push_back({std::move(key), std::move(label), Kind::String, std::move(def), 0, 0, {}});
    return *this;
}

SettingsSchema& SettingsSchema::Enum(std::string key, std::vector<std::string> options, size_t def, std::string label) {
    m_entries.push_back({std::move(key), std::move(label), Kind::Enum, def, 0, 0, std::move(options)});
    return *this;
}

SettingsSchema& SettingsSchema::Path(std::string key, std::string def, std::string label) {
    m_entries.push_back({std::move(key), std::move(label), Kind::Path, std::move(def), 0, 0, {}});
    return *this;
}

void ApplyDefaults(const SettingsSchema& schema, Settings& settings) {
    using Kind = SettingsSchema::Kind;
    for (const auto& e : schema.Entries()) {
        switch (e.kind) {
        case Kind::Bool:
            if (!settings.Has(e.key)) settings.Set(e.key, std::get<bool>(e.def));
            break;
        case Kind::Int:
            if (!settings.Has(e.key)) settings.Set(e.key, std::get<int64_t>(e.def));
            break;
        case Kind::Double:
            if (!settings.Has(e.key)) settings.Set(e.key, std::get<double>(e.def));
            break;
        case Kind::String:
        case Kind::Path:
            if (!settings.Has(e.key)) settings.Set(e.key, std::get<std::string>(e.def));
            break;
        case Kind::Enum:
            if (!settings.Has(e.key)) {
                size_t index = std::get<size_t>(e.def);
                if (index < e.options.size()) settings.Set(e.key, e.options[index]);
            }
            break;
        }
    }
}

}
