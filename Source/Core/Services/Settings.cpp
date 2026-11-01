#include <Onyx/Core/Services/Settings.h>

#include <Onyx/Core/Services/Paths.h>

#include <toml++/toml.hpp>

#include <fstream>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Onyx::Services {

namespace {

std::pair<std::string, std::string> SplitKey(std::string_view key) {
    size_t dot = key.find('.');
    if (dot == std::string_view::npos) {
        return {"", std::string(key)};
    }
    return {std::string(key.substr(0, dot)), std::string(key.substr(dot + 1))};
}

template <class T>
std::optional<T> GetExact(const toml::table& root, std::string_view key) {
    auto [tableName, name] = SplitKey(key);
    if (tableName.empty()) {
        return root[name].template value_exact<T>();
    }
    if (const auto* table = root[tableName].as_table()) {
        return (*table)[name].template value_exact<T>();
    }
    return std::nullopt;
}

template <class T>
void SetValue(toml::table& root, std::string_view key, T value) {
    auto [tableName, name] = SplitKey(key);
    if (tableName.empty()) {
        root.insert_or_assign(name, std::move(value));
        return;
    }
    if (!root[tableName].is_table()) {
        root.insert_or_assign(tableName, toml::table());
    }
    if (auto* table = root[tableName].as_table()) {
        table->insert_or_assign(name, std::move(value));
    }
}

bool ReplaceFile(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    if (!ec) return true;
#if defined(_WIN32)
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
#endif
    return false;
}

}

class Settings::Impl {
public:
    toml::table tbl;
    std::filesystem::path path;
    bool dirty = false;
    std::optional<std::string> loadError;
};

Settings::Settings() : impl_(std::make_unique<Impl>()) {}

Settings::~Settings() = default;
Settings::Settings(Settings&&) noexcept = default;
Settings& Settings::operator=(Settings&&) noexcept = default;

Settings Settings::Empty() {
    return Settings();
}

Settings Settings::Load(const std::filesystem::path& file) {
    Settings s;
    s.impl_->path = file;

    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        return s;
    }

    std::string content;
    {
        std::ifstream in(file, std::ios::binary);
        if (!in.is_open()) {
            s.impl_->loadError = "settings.read: cannot open " + Paths::ToUtf8(file) + " for reading";
            return s;
        }
        std::stringstream ss;
        ss << in.rdbuf();
        content = ss.str();
    }

    try {
        s.impl_->tbl = toml::parse(content);
    } catch (const toml::parse_error& e) {
        s.impl_->tbl = toml::table();
        std::string message(e.description());
        auto broken = file.parent_path() / file.stem();
        broken += ".broken.toml";
        if (!std::filesystem::exists(broken, ec)) {
            std::error_code copyEc;
            std::filesystem::copy_file(file, broken, copyEc);
            if (copyEc) {
                message += " (preserving a copy failed: " + copyEc.message() + ")";
            }
        }
        s.impl_->loadError = std::move(message);
    }

    return s;
}

Result<void> Settings::Save() {
    if (impl_->loadError) {
        return Result<void>::Fail({"settings.broken",
            "settings file failed to parse and was preserved: " + *impl_->loadError, 0});
    }

    const auto& path = impl_->path;
    if (path.empty()) {
        return Result<void>::Fail({"settings.nopath", "settings has no file path", 0});
    }

    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f.is_open()) {
            return Result<void>::Fail({"settings.write", "cannot open " + Paths::ToUtf8(tmp), 0});
        }
        f << impl_->tbl;
        f.close();
        if (!f) {
            std::filesystem::remove(tmp, ec);
            return Result<void>::Fail({"settings.write", "cannot write " + Paths::ToUtf8(tmp), 0});
        }
    }

    if (!ReplaceFile(tmp, path)) {
        std::filesystem::remove(tmp, ec);
        return Result<void>::Fail({"settings.replace", "cannot replace " + Paths::ToUtf8(path), 0});
    }

    impl_->dirty = false;
    return Result<void>::Ok();
}

Result<void> Settings::SaveIfDirty() {
    if (!impl_->dirty) return Result<void>::Ok();
    return Save();
}

bool Settings::Has(std::string_view key) const {
    auto [tableName, name] = SplitKey(key);
    if (tableName.empty()) {
        return impl_->tbl.contains(name);
    }
    if (const auto* table = impl_->tbl[tableName].as_table()) {
        return table->contains(name);
    }
    return false;
}

std::optional<bool> Settings::GetBool(std::string_view key) const {
    return GetExact<bool>(impl_->tbl, key);
}

std::optional<int64_t> Settings::GetInt(std::string_view key) const {
    return GetExact<int64_t>(impl_->tbl, key);
}

std::optional<double> Settings::GetDouble(std::string_view key) const {
    return GetExact<double>(impl_->tbl, key);
}

std::optional<std::string> Settings::GetString(std::string_view key) const {
    return GetExact<std::string>(impl_->tbl, key);
}

void Settings::Set(std::string_view key, bool v) {
    impl_->dirty = true;
    SetValue(impl_->tbl, key, v);
}

void Settings::Set(std::string_view key, int64_t v) {
    impl_->dirty = true;
    SetValue(impl_->tbl, key, v);
}

void Settings::Set(std::string_view key, double v) {
    impl_->dirty = true;
    SetValue(impl_->tbl, key, v);
}

void Settings::Set(std::string_view key, std::string v) {
    impl_->dirty = true;
    SetValue(impl_->tbl, key, std::move(v));
}

void Settings::Set(std::string_view key, std::string_view v) {
    Set(key, std::string(v));
}

void Settings::Set(std::string_view key, const char* v) {
    Set(key, std::string(v));
}

Settings Settings::Clone() const {
    Settings copy;
    *copy.impl_ = *impl_;
    return copy;
}

bool Settings::Dirty() const { return impl_->dirty; }

const std::filesystem::path& Settings::Path() const { return impl_->path; }

std::optional<std::string> Settings::LoadError() const { return impl_->loadError; }

void Settings::ClearLoadError() { impl_->loadError.reset(); }

}
