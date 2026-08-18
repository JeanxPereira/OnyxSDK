#include <Onyx/Services/EventBus.h>

#include <Onyx/Services/Logger.h>

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
        // Snapshot only the *ids* registered for this tag right now — not
        // the handlers themselves. Each handler is re-looked-up by id and
        // copied individually, right before it is invoked, with the lock
        // held only for that lookup+copy. This way, if an earlier handler
        // in this loop unsubscribes a later one, the later id no longer
        // resolves and is skipped — even though both belong to the event
        // currently being dispatched. Copying one std::function at a time
        // (instead of the whole vector up front) is what makes that
        // possible: the vector holding a stale copy of an unsubscribed
        // handler was exactly the bug this replaces.
        std::vector<uint64_t> ids;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_handlers.find(queued.tag);
            if (it != m_handlers.end()) {
                ids.reserve(it->second.size());
                for (const auto& h : it->second)
                    ids.push_back(h.id);
            }
        }

        for (uint64_t id : ids) {
            std::function<void(const void*)> invoke;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto it = m_handlers.find(queued.tag);
                if (it == m_handlers.end())
                    break; // whole tag was removed; nothing left to run
                auto hit = std::find_if(it->second.begin(), it->second.end(),
                                         [id](const Detail::EventHandlerEntry& e) { return e.id == id; });
                if (hit == it->second.end())
                    continue; // unsubscribed since the id snapshot above
                invoke = hit->invoke;
            }

            // A throwing handler must not skip the remaining handlers for
            // this event, nor the remaining queued events.
            try {
                invoke(queued.data.get());
            } catch (...) {
                LOG_ERR("[EventBus] handler threw during Pump; continuing");
            }
        }
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
