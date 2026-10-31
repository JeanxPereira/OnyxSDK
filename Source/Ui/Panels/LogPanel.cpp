#include "Onyx/Ui/Panels/LogPanel.h"

#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/Widgets.h>

#include <imgui.h>

#include <chrono>
#include <ctime>
#include <format>

namespace Onyx::Ui {

namespace {

constexpr const char* kLevelNames[] = {"Trace", "Debug", "Info", "Warn", "Error"};

// HH:MM:SS in the local time zone.
std::string LocalClock(std::chrono::system_clock::time_point time) {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(time);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    char text[16] = {};
    std::strftime(text, sizeof(text), "%H:%M:%S", &local);
    return text;
}

}

LogPanel::LogPanel(Services::Log& log) : m_log(log) {
    m_sink = m_log.AddSink([this](const Services::LogEntry& entry) { Push(entry); }, Services::LogLevel::Trace);
}

LogPanel::~LogPanel() {
    m_log.RemoveSink(m_sink);
}

void LogPanel::Push(const Services::LogEntry& entry) {
    Line line;
    line.level = entry.level;
    line.text = std::format("{} {:<5} [{}] {}", LocalClock(entry.time), Services::LogLevelName(entry.level),
                            entry.category, entry.message);
    std::lock_guard lock(m_mutex);
    if (m_lines.size() == kCapacity) m_lines.pop_front();
    m_lines.push_back(std::move(line));
    ++m_version;
}

size_t LogPanel::Size() const {
    std::lock_guard lock(m_mutex);
    return m_lines.size();
}

std::vector<std::string> LogPanel::Lines() const {
    std::lock_guard lock(m_mutex);
    std::vector<std::string> out;
    out.reserve(m_lines.size());
    for (const Line& line : m_lines) out.push_back(line.text);
    return out;
}

void LogPanel::Clear() {
    std::lock_guard lock(m_mutex);
    m_lines.clear();
    ++m_version;
}

void LogPanel::Draw(UiContext&) {
    int level = static_cast<int>(minLevel);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    if (ImGui::Combo("Level", &level, kLevelNames, 5)) minLevel = static_cast<Services::LogLevel>(level);
    ImGui::SameLine();
    if (Widgets::ToolbarButton("Clear")) Clear();
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &autoScroll);
    ImGui::Separator();

    if (!ImGui::BeginChild("lines", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::EndChild();
        return;
    }
    {
        std::lock_guard lock(m_mutex);
        const bool unfiltered = minLevel == Services::LogLevel::Trace;
        if (!unfiltered && (m_filteredVersion != m_version || m_filteredLevel != minLevel)) {
            m_filtered.clear();
            for (size_t i = 0; i < m_lines.size(); ++i) {
                if (m_lines[i].level >= minLevel) m_filtered.push_back(i);
            }
            m_filteredVersion = m_version;
            m_filteredLevel = minLevel;
        }
        const size_t count = unfiltered ? m_lines.size() : m_filtered.size();
        Widgets::ForEachVisible(count, ImGui::GetTextLineHeightWithSpacing(), [&](size_t i) {
            const Line& line = m_lines[unfiltered ? i : m_filtered[i]];
            ImGui::TextUnformatted(line.text.c_str());
        });
        if (autoScroll && m_shownVersion != m_version) ImGui::SetScrollHereY(1.0f);
        m_shownVersion = m_version;
    }
    ImGui::EndChild();
}

}
