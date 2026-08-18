#include <Onyx/Services/Diagnostics.h>

namespace Onyx::Services {

void DiagSink::Report(Diag d) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_diags.push_back(std::move(d));
}

std::vector<Diag> DiagSink::Drain() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<Diag> out;
    out.swap(m_diags);
    return out;
}

size_t DiagSink::Count(Severity atLeast) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    size_t count = 0;
    for (const auto& d : m_diags) {
        if (d.severity >= atLeast) {
            ++count;
        }
    }
    return count;
}

bool DiagSink::HasErrors() const {
    return Count(Severity::Error) > 0;
}

} // namespace Onyx::Services
