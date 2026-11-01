#include "Onyx/Shell/Recents.h"

#include <algorithm>
#include <cwctype>

#include "Onyx/Core/Services/Paths.h"

namespace Onyx::Shell {

namespace {

constexpr const char* kKey = "shell.recents";

std::string Escape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '%': out += "%25"; break;
        case ';': out += "%3B"; break;
        case '|': out += "%7C"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::string Unescape(std::string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const std::string_view code = text.substr(i + 1, 2);
            if (code == "25") { out += '%'; i += 2; continue; }
            if (code == "3B") { out += ';'; i += 2; continue; }
            if (code == "7C") { out += '|'; i += 2; continue; }
        }
        out += text[i];
    }
    return out;
}

bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b) {
#if defined(_WIN32)
    std::wstring left = a.lexically_normal().native();
    std::wstring right = b.lexically_normal().native();
    for (wchar_t& c : left) c = static_cast<wchar_t>(std::towlower(c));
    for (wchar_t& c : right) c = static_cast<wchar_t>(std::towlower(c));
    return left == right;
#else
    return a.lexically_normal() == b.lexically_normal();
#endif
}

RecentEntry MakeEntry(const std::filesystem::path& path, std::string_view moduleId) {
    return RecentEntry{path, std::string(moduleId), Services::Paths::ToUtf8(path.filename())};
}

}

Recents::Recents(Services::Settings& settings) : m_settings(settings) {
    const std::optional<std::string> stored = settings.GetString(kKey);
    if (!stored) {
        return;
    }
    std::string_view rest = *stored;
    while (!rest.empty() && m_entries.size() < kMaxRecents) {
        const size_t end = rest.find(';');
        const std::string_view item = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
        const size_t bar = item.find('|');
        if (item.empty() || bar == std::string_view::npos || bar == 0) {
            continue;
        }
        m_entries.push_back(MakeEntry(Services::Paths::FromUtf8(Unescape(item.substr(0, bar))), Unescape(item.substr(bar + 1))));
    }
}

void Recents::Add(const std::filesystem::path& path, std::string_view moduleId) {
    std::erase_if(m_entries, [&](const RecentEntry& entry) { return SamePath(entry.path, path); });
    m_entries.insert(m_entries.begin(), MakeEntry(path, moduleId));
    if (m_entries.size() > kMaxRecents) {
        m_entries.resize(kMaxRecents);
    }
    Store();
}

void Recents::Clear() {
    m_entries.clear();
    Store();
}

void Recents::Store() {
    std::string text;
    for (const RecentEntry& entry : m_entries) {
        if (!text.empty()) {
            text += ';';
        }
        text += Escape(Services::Paths::ToUtf8(entry.path)) + '|' + Escape(entry.moduleId);
    }
    m_settings.Set(kKey, text);
}

}
