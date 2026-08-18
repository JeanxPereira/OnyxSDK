#pragma once

// Diagnostics as data — thread-safe collection of diagnostic messages.
//
// Producers call Report() from any thread with a Diag message.
// The owner thread calls Drain() to retrieve and clear all collected
// diagnostics. Never throws, never logs by itself.

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
    std::string code;      // namespaced: "gowr.mesh.lod-missing"
    std::string message;
    std::optional<ByteRef> at;
};

// Thread-safe collector. Producers Report() from any thread; the owner
// Drain()s on its own thread. Never throws, never logs by itself.
class DiagSink {
public:
    void Report(Diag d);
    std::vector<Diag> Drain();                 // returns and clears
    size_t Count(Severity atLeast = Severity::Info) const;
    bool   HasErrors() const;                  // Count(Error) > 0

private:
    mutable std::mutex m_mutex;
    std::vector<Diag> m_diags;
};

} // namespace Onyx::Services
