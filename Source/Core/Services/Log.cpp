#include <Onyx/Core/Services/Log.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <memory>
#include <system_error>

namespace fs = std::filesystem;

namespace Onyx::Services {

const char* LogLevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "INFO";
}

Log& Log::Default() {
    static Log instance;
    return instance;
}

void Log::SetMinLevel(LogLevel level) {
    m_minLevel.store(level, std::memory_order_relaxed);
}

LogLevel Log::MinLevel() const {
    return m_minLevel.load(std::memory_order_relaxed);
}

uint64_t Log::AddSink(Sink sink, LogLevel min) {
    std::lock_guard lock(m_mutex);
    uint64_t id = m_nextId++;
    m_sinks.push_back({id, min, std::move(sink)});
    return id;
}

void Log::RemoveSink(uint64_t id) {
    std::lock_guard lock(m_mutex);
    std::erase_if(m_sinks, [id](const SinkSlot& s) { return s.id == id; });
}

void Log::SetRingCapacity(size_t capacity) {
    std::lock_guard lock(m_mutex);
    m_ringCapacity = capacity;
    while (m_ring.size() > m_ringCapacity) m_ring.pop_front();
}

std::vector<LogEntry> Log::Entries() const {
    std::lock_guard lock(m_mutex);
    return std::vector<LogEntry>(m_ring.begin(), m_ring.end());
}

void Log::Clear() {
    std::lock_guard lock(m_mutex);
    m_ring.clear();
}

void Log::Write(LogLevel level, std::string_view category, std::string_view message) {
    if (level < MinLevel()) return;

    LogEntry entry{level, std::chrono::system_clock::now(), std::string(category), std::string(message)};
    std::vector<SinkSlot> targets;
    {
        std::lock_guard lock(m_mutex);
        m_ring.push_back(entry);
        while (m_ring.size() > m_ringCapacity) m_ring.pop_front();
        for (const SinkSlot& s : m_sinks)
            if (level >= s.min) targets.push_back(s);
    }
    for (const SinkSlot& s : targets) s.fn(entry);
}

namespace {

std::string FormatLine(const LogEntry& e) {
    auto secs = std::chrono::floor<std::chrono::seconds>(e.time);
    std::string line = std::format("{:%F %T} [{}]", secs, LogLevelName(e.level));
    if (!e.category.empty()) line += std::format(" [{}]", e.category);
    line += ' ';
    line += e.message;
    line += '\n';
    return line;
}

class FileSink {
public:
    FileSink(fs::path path, size_t maxBytes, int rotations)
        : m_path(std::move(path)), m_maxBytes(maxBytes), m_rotations(rotations) {
        std::error_code ec;
        if (m_path.has_parent_path()) fs::create_directories(m_path.parent_path(), ec);
        Open();
    }

    void operator()(const LogEntry& e) {
        std::string line = FormatLine(e);
        std::lock_guard lock(m_mutex);
        if (m_maxBytes > 0 && m_size > 0 && m_size + line.size() > m_maxBytes) Rotate();
        if (!m_out.is_open()) return;
        m_out.write(line.data(), static_cast<std::streamsize>(line.size()));
        m_size += line.size();
    }

private:
    void Open() {
        std::error_code ec;
        m_size = fs::exists(m_path, ec) ? static_cast<size_t>(fs::file_size(m_path, ec)) : 0;
        m_out.open(m_path, std::ios::app | std::ios::binary);
    }

    fs::path Numbered(int n) const {
        fs::path p = m_path;
        p += "." + std::to_string(n);
        return p;
    }

    void Rotate() {
        m_out.close();
        std::error_code ec;
        if (m_rotations > 0) {
            fs::remove(Numbered(m_rotations), ec);
            for (int i = m_rotations - 1; i >= 1; --i) fs::rename(Numbered(i), Numbered(i + 1), ec);
            fs::rename(m_path, Numbered(1), ec);
        } else {
            fs::remove(m_path, ec);
        }
        m_out.open(m_path, std::ios::trunc | std::ios::binary);
        m_size = 0;
    }

    std::mutex m_mutex;
    fs::path m_path;
    size_t m_maxBytes;
    int m_rotations;
    std::ofstream m_out;
    size_t m_size = 0;
};

}

uint64_t InstallStderrSink(Log& log) {
    return log.AddSink([](const LogEntry& e) {
        std::string line = FormatLine(e);
        std::fwrite(line.data(), 1, line.size(), stderr);
    });
}

uint64_t InstallFileSink(Log& log, const fs::path& path, size_t maxBytes, int rotations) {
    auto sink = std::make_shared<FileSink>(path, maxBytes, rotations);
    return log.AddSink([sink](const LogEntry& e) { (*sink)(e); });
}

}
