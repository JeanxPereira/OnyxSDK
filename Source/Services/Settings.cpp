#include <Onyx/Services/Settings.h>
#include <toml++/toml.hpp>

#include <fstream>
#include <memory>
#include <sstream>
#include <string_view>

namespace Onyx::Services {

namespace {

// Split dotted key into table name and actual key.
// "a.b.c" → ("a", "b.c")
// "key" → ("", "key")
std::pair<std::string, std::string> SplitKey(std::string_view key) {
    size_t dot_pos = key.find('.');
    if (dot_pos == std::string::npos) {
        return {"", std::string(key)};
    }
    return {std::string(key.substr(0, dot_pos)), std::string(key.substr(dot_pos + 1))};
}

}  // namespace

class Settings::Impl {
 public:
    toml::table tbl;
    std::filesystem::path path;
    bool dirty = false;
};

Settings::Settings() : impl_(std::make_unique<Impl>()) {}

Settings::~Settings() = default;

Settings Settings::Load(const std::filesystem::path& file) {
    Settings s;
    s.impl_->path = file;

    if (!std::filesystem::exists(file)) {
        return s;
    }

    try {
        s.impl_->tbl = toml::parse_file(file.string());
    } catch (const toml::parse_error&) {
        // Invalid TOML → return empty settings
    }

    return s;
}

bool Settings::Save() {
    if (!impl_) return false;

    try {
        std::ofstream f(impl_->path);
        if (!f.is_open()) return false;

        f << impl_->tbl;
        f.close();

        impl_->dirty = false;
        return true;
    } catch (...) {
        return false;
    }
}

std::optional<bool> Settings::GetBool(std::string_view key) const {
    auto [table_name, actual_key] = SplitKey(key);

    if (table_name.empty()) {
        // Root-level key
        if (auto v = impl_->tbl[actual_key].value_exact<bool>()) {
            return *v;
        }
    } else {
        // Scoped key
        if (auto* tbl = impl_->tbl[table_name].as_table()) {
            if (auto v = (*tbl)[actual_key].value_exact<bool>()) {
                return *v;
            }
        }
    }

    return std::nullopt;
}

std::optional<int64_t> Settings::GetInt(std::string_view key) const {
    auto [table_name, actual_key] = SplitKey(key);

    if (table_name.empty()) {
        // Root-level key
        if (auto v = impl_->tbl[actual_key].value_exact<int64_t>()) {
            return *v;
        }
    } else {
        // Scoped key
        if (auto* tbl = impl_->tbl[table_name].as_table()) {
            if (auto v = (*tbl)[actual_key].value_exact<int64_t>()) {
                return *v;
            }
        }
    }

    return std::nullopt;
}

std::optional<double> Settings::GetDouble(std::string_view key) const {
    auto [table_name, actual_key] = SplitKey(key);

    if (table_name.empty()) {
        // Root-level key
        if (auto v = impl_->tbl[actual_key].value_exact<double>()) {
            return *v;
        }
    } else {
        // Scoped key
        if (auto* tbl = impl_->tbl[table_name].as_table()) {
            if (auto v = (*tbl)[actual_key].value_exact<double>()) {
                return *v;
            }
        }
    }

    return std::nullopt;
}

std::optional<std::string> Settings::GetString(std::string_view key) const {
    auto [table_name, actual_key] = SplitKey(key);

    if (table_name.empty()) {
        // Root-level key
        if (auto v = impl_->tbl[actual_key].value_exact<std::string>()) {
            return *v;
        }
    } else {
        // Scoped key
        if (auto* tbl = impl_->tbl[table_name].as_table()) {
            if (auto v = (*tbl)[actual_key].value_exact<std::string>()) {
                return *v;
            }
        }
    }

    return std::nullopt;
}

void Settings::Set(std::string_view key, bool v) {
    auto [table_name, actual_key] = SplitKey(key);
    impl_->dirty = true;

    if (table_name.empty()) {
        impl_->tbl.insert_or_assign(actual_key, v);
    } else {
        if (!impl_->tbl.contains(table_name)) {
            impl_->tbl.insert_or_assign(table_name, toml::table());
        }
        auto* tbl = impl_->tbl[table_name].as_table();
        if (tbl) {
            tbl->insert_or_assign(actual_key, v);
        }
    }
}

void Settings::Set(std::string_view key, int64_t v) {
    auto [table_name, actual_key] = SplitKey(key);
    impl_->dirty = true;

    if (table_name.empty()) {
        impl_->tbl.insert_or_assign(actual_key, v);
    } else {
        if (!impl_->tbl.contains(table_name)) {
            impl_->tbl.insert_or_assign(table_name, toml::table());
        }
        auto* tbl = impl_->tbl[table_name].as_table();
        if (tbl) {
            tbl->insert_or_assign(actual_key, v);
        }
    }
}

void Settings::Set(std::string_view key, double v) {
    auto [table_name, actual_key] = SplitKey(key);
    impl_->dirty = true;

    if (table_name.empty()) {
        impl_->tbl.insert_or_assign(actual_key, v);
    } else {
        if (!impl_->tbl.contains(table_name)) {
            impl_->tbl.insert_or_assign(table_name, toml::table());
        }
        auto* tbl = impl_->tbl[table_name].as_table();
        if (tbl) {
            tbl->insert_or_assign(actual_key, v);
        }
    }
}

void Settings::Set(std::string_view key, std::string v) {
    auto [table_name, actual_key] = SplitKey(key);
    impl_->dirty = true;

    if (table_name.empty()) {
        impl_->tbl.insert_or_assign(actual_key, std::move(v));
    } else {
        if (!impl_->tbl.contains(table_name)) {
            impl_->tbl.insert_or_assign(table_name, toml::table());
        }
        auto* tbl = impl_->tbl[table_name].as_table();
        if (tbl) {
            tbl->insert_or_assign(actual_key, std::move(v));
        }
    }
}

bool Settings::Dirty() const { return impl_->dirty; }

const std::filesystem::path& Settings::Path() const { return impl_->path; }

}  // namespace Onyx::Services
