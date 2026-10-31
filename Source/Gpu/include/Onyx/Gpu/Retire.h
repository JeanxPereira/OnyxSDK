#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>

namespace Onyx::Gpu {

using FenceValue = uint64_t;

class RetireQueue {
public:
    void Enqueue(std::function<void()> deleter, FenceValue readyAfter);
    void Collect(FenceValue completed);
    void Drain();
    size_t Pending() const;

private:
    struct Entry {
        std::function<void()> deleter;
        FenceValue readyAfter = 0;
    };

    mutable std::mutex m_mutex;
    std::deque<Entry> m_entries;
};

}
