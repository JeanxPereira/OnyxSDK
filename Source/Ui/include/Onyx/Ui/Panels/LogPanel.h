#pragma once

#include <Onyx/Core/Services/Log.h>
#include <Onyx/Ui/Panel.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace Onyx::Ui {

// Keeps the last kCapacity log lines of a Log in its own ring. Each line is formatted once, on
// the thread that wrote it; drawing reads the ring in place.
class LogPanel final : public IPanel {
public:
    static constexpr size_t kCapacity = 4096;

    explicit LogPanel(Services::Log& log = Services::Log::Default());
    ~LogPanel() override;

    LogPanel(const LogPanel&) = delete;
    LogPanel& operator=(const LogPanel&) = delete;

    std::string_view Id() const override { return "onyx.log"; }
    std::string_view Title() const override { return "Log"; }
    void Draw(UiContext& ui) override;

    size_t Size() const;
    // A copy of the formatted lines, oldest first.
    std::vector<std::string> Lines() const;
    void Clear();

    Services::LogLevel minLevel = Services::LogLevel::Trace;
    bool autoScroll = true;

private:
    struct Line {
        Services::LogLevel level;
        std::string text;
    };

    void Push(const Services::LogEntry& entry);

    Services::Log& m_log;
    uint64_t m_sink = 0;
    mutable std::mutex m_mutex;
    std::deque<Line> m_lines;
    uint64_t m_version = 0;
    uint64_t m_shownVersion = 0;
    std::vector<size_t> m_filtered;
    uint64_t m_filteredVersion = ~uint64_t{0};
    Services::LogLevel m_filteredLevel = Services::LogLevel::Trace;
};

}
