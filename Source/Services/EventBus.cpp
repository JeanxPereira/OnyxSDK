#include <Onyx/Services/EventBus.h>

#include <algorithm>

namespace Onyx::Services {

// ── EventBus ────────────────────────────────────────────────────────────

EventBus::EventBus() : m_alive(std::make_shared<std::atomic<bool>>(true)) {}

EventBus::~EventBus() {
    // Flip the shared flag first: any Subscription destroyed after this
    // point (including ones sequenced-after on another thread that has
    // already synchronized with the destructor call) sees the bus as
    // dead and skips calling back into it.
    m_alive->store(false, std::memory_order_release);
}

void EventBus::Unsubscribe(const void* tag, uint64_t id) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_handlers.find(tag);
    if (it == m_handlers.end())
        return;

    auto& handlers = it->second;
    handlers.erase(
        std::remove_if(handlers.begin(), handlers.end(),
                        [id](const Detail::EventHandlerEntry& e) { return e.id == id; }),
        handlers.end());
}

void EventBus::Pump() {
    // Move the whole pending queue out under the lock in one step, then
    // dispatch unlocked. A handler that calls Post() during this loop
    // appends to m_queue (the member, now empty), not to `queue` — so
    // that event is left for the next Pump(), not this one.
    std::deque<Detail::QueuedEvent> queue;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        queue.swap(m_queue);
    }

    for (auto& queued : queue) {
        // Copy the handler list for this tag under the lock, then invoke
        // the copies unlocked — no handler ever runs while m_mutex is
        // held, and a handler is free to subscribe/unsubscribe without
        // deadlocking.
        std::vector<Detail::EventHandlerEntry> handlers;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_handlers.find(queued.tag);
            if (it != m_handlers.end())
                handlers = it->second;
        }

        for (auto& handler : handlers)
            handler.invoke(queued.data.get());
    }
}

size_t EventBus::PendingEvents() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size();
}

// ── Subscription ────────────────────────────────────────────────────────

Subscription::~Subscription() {
    Reset();
}

Subscription::Subscription(Subscription&& other) noexcept
    : m_bus(other.m_bus), m_tag(other.m_tag), m_id(other.m_id),
      m_busAlive(std::move(other.m_busAlive)) {
    other.m_bus = nullptr;
    other.m_tag = nullptr;
    other.m_id = 0;
}

Subscription& Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) {
        Reset(); // this Subscription's own handler must still be dropped
        m_bus = other.m_bus;
        m_tag = other.m_tag;
        m_id = other.m_id;
        m_busAlive = std::move(other.m_busAlive);
        other.m_bus = nullptr;
        other.m_tag = nullptr;
        other.m_id = 0;
    }
    return *this;
}

void Subscription::Reset() noexcept {
    if (m_bus && m_busAlive && m_busAlive->load(std::memory_order_acquire)) {
        m_bus->Unsubscribe(m_tag, m_id);
    }
    m_bus = nullptr;
    m_tag = nullptr;
    m_id = 0;
    m_busAlive.reset();
}

} // namespace Onyx::Services
