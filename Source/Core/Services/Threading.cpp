#include <Onyx/Core/Services/Threading.h>

#include <atomic>
#include <thread>

namespace Onyx::Services {

namespace {

std::atomic<bool> g_marked{false};
std::thread::id g_mainId{};

}

void MarkMainThread() {
    g_mainId = std::this_thread::get_id();
    g_marked.store(true, std::memory_order_release);
}

bool IsMainThread() {
    if (!g_marked.load(std::memory_order_acquire)) return false;
    return std::this_thread::get_id() == g_mainId;
}

}
