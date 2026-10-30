#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace Onyx::Services {

enum class Severity : uint8_t { Info, Warning, Error };

struct ByteRef {
    std::string file;
    uint64_t offset = 0;
};

struct Diag {
    Severity severity = Severity::Info;
    std::string code;
    std::string message;
    std::optional<ByteRef> at;
};

// Thread-safe collector. Producers Report() from any thread; the owner Drain()s
// on its own thread. Never throws, never logs by itself.
class DiagSink {
public:
    void Report(Diag d);
    std::vector<Diag> Drain();
    size_t Count(Severity atLeast = Severity::Info) const;
    bool HasErrors() const;

    // Non-destructive copy of everything collected so far.
    std::vector<Diag> Snapshot() const;

private:
    mutable std::mutex m_mutex;
    std::vector<Diag> m_diags;
};

}
