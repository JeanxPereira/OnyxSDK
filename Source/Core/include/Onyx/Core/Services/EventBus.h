#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

// Threading contract:
//   - Post<E>() is callable from any thread; it only queues a copy of the event.
//   - Pump() dispatches queued events in FIFO order on the calling thread. It
//     takes the whole queue out of the lock in one step and dispatches without
//     holding it, so an event Posted by a handler runs on the next Pump().
//   - A Subscription outliving its EventBus is a safe no-op on destruction.

namespace Onyx::Services {

namespace Detail {

struct EventHandlerEntry {
    uint64_t id = 0;
    std::function<void(const void*)> invoke;
};

struct QueuedEvent {
    const void* tag = nullptr;
    std::shared_ptr<void> data;
};

}

class EventBus;

// Move-only RAII handle; destruction unsubscribes the handler.
class Subscription {
public:
    Subscription() = default;
    ~Subscription();

    Subscription(Subscription&& other) noexcept;
    Subscription& operator=(Subscription&& other) noexcept;

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

private:
    friend class EventBus;

    Subscription(EventBus* bus, const void* tag, uint64_t id,
                 std::shared_ptr<std::atomic<bool>> busAlive) noexcept
        : m_bus(bus), m_tag(tag), m_id(id), m_busAlive(std::move(busAlive)) {}

    void Reset() noexcept;

    EventBus* m_bus = nullptr;
    const void* m_tag = nullptr;
    uint64_t m_id = 0;
    std::shared_ptr<std::atomic<bool>> m_busAlive;
};

// Owned by its composition root; not a singleton. A bus must not be destroyed
// concurrently with a Subscription's destruction on another thread.
class EventBus {
public:
    EventBus();
    ~EventBus();

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    // Unique per instantiation of E, stable for the program's lifetime.
    template <class E>
    static const void* TypeTag() {
        static char tag;
        return &tag;
    }

    // `fn` is only ever invoked from Pump().
    template <class E>
    Subscription On(std::function<void(const E&)> fn) {
        const void* tag = TypeTag<E>();
        uint64_t id;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            id = m_nextId++;
            Detail::EventHandlerEntry entry;
            entry.id = id;
            entry.invoke = [fn = std::move(fn)](const void* ev) {
                fn(*static_cast<const E*>(ev));
            };
            m_handlers[tag].push_back(std::move(entry));
        }
        return Subscription(this, tag, id, m_alive);
    }

    template <class E>
    void Post(E ev) {
        auto data = std::make_shared<E>(std::move(ev));
        const void* tag = TypeTag<E>();
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(Detail::QueuedEvent{tag, std::move(data)});
    }

    // A handler unsubscribed by an earlier handler is skipped immediately, even
    // for the event currently being dispatched. A throwing handler is caught
    // and logged; remaining handlers and events still run.
    void Pump();

    size_t PendingEvents() const;

private:
    friend class Subscription;

    void Unsubscribe(const void* tag, uint64_t id);

    mutable std::mutex m_mutex;
    uint64_t m_nextId = 1;
    std::map<const void*, std::vector<Detail::EventHandlerEntry>> m_handlers;
    std::deque<Detail::QueuedEvent> m_queue;
    std::shared_ptr<std::atomic<bool>> m_alive;
};

}
