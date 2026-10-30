#include <Onyx/Core/Services/Threading.h>

#include <atomic>
#include <thread>

namespace Onyx::Services {

namespace {

std::atomic<int> g_state{0};
std::thread::id g_mainId{};

}

void MarkMainThread() {
    int expected = 0;
    if (!g_state.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) return;
    g_mainId = std::this_thread::get_id();
    g_state.store(2, std::memory_order_release);
}

bool IsMainThread() {
    if (g_state.load(std::memory_order_acquire) != 2) return false;
    return std::this_thread::get_id() == g_mainId;
}

}
