#include "Onyx/Gpu/Retire.h"

#include <vector>

namespace Onyx::Gpu {

void RetireQueue::Enqueue(std::function<void()> deleter, FenceValue readyAfter) {
    std::lock_guard lock(m_mutex);
    m_entries.push_back({std::move(deleter), readyAfter});
}

void RetireQueue::Collect(FenceValue completed) {
    std::vector<std::function<void()>> ready;
    {
        std::lock_guard lock(m_mutex);
        for (auto it = m_entries.begin(); it != m_entries.end();) {
            if (it->readyAfter <= completed) {
                ready.push_back(std::move(it->deleter));
                it = m_entries.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& deleter : ready) {
        deleter();
    }
}

void RetireQueue::Drain() {
    for (;;) {
        std::deque<Entry> batch;
        {
            std::lock_guard lock(m_mutex);
            batch.swap(m_entries);
        }
        if (batch.empty()) {
            return;
        }
        for (auto& entry : batch) {
            entry.deleter();
        }
    }
}

size_t RetireQueue::Pending() const {
    std::lock_guard lock(m_mutex);
    return m_entries.size();
}

}
