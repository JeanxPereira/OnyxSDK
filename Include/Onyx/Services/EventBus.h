#pragma once

// EventBus — Workspace-ownable, non-singleton pub/sub event bus.
//
// This replaces the global Onyx::Services::EventManager in a later
// milestone: an EventBus instance is owned by its composition root (a
// Workspace, from M3) and passed around by reference — never a
// process-wide singleton.
//
// Threading contract:
//   - Post<E>() may be called from any thread. It only appends a copy of
//     the event to an internal FIFO queue under a mutex; it never invokes
//     a handler itself.
//   - Pump() dispatches queued events, in FIFO order, on the thread that
//     calls it. Handlers are invoked only from inside Pump(), never from
//     Post().
//   - Pump() moves the entire pending queue out from under the mutex in
//     one step, then dispatches without holding it. No handler is ever
//     invoked while the bus's mutex is held. A consequence: if a handler
//     calls Post() while Pump() is dispatching, that new event is queued
//     but is NOT part of the batch currently being drained — it is
//     dispatched by the *next* call to Pump(), not the current one.
//
// Subscription is RAII and move-only: its destructor unsubscribes the
// handler. A Subscription that outlives the EventBus it was created from
// is a safe no-op on destruction (it never dereferences the dead bus) —
// this is implemented via a flag shared between the bus and every
// Subscription it hands out, which the bus flips in its own destructor.
//
// Type identity per event type is established without RTTI: TypeTag<E>()
// returns the address of a function-local static that is unique per
// template instantiation of E, and stable for the life of the program.

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

namespace Onyx::Services {

namespace Detail {

// One handler registered against one event type tag.
struct EventHandlerEntry {
    uint64_t id = 0;
    std::function<void(const void*)> invoke; // receives a `const E*`
};

// One queued, type-erased event awaiting a Pump().
struct QueuedEvent {
    const void* tag = nullptr;
    std::shared_ptr<void> data; // owns the `E` instance
};

} // namespace Detail

class EventBus;

// RAII handle for one EventBus subscription. Move-only; destruction
// unsubscribes the handler from the bus that produced it. Safe (a no-op)
// to destroy after that EventBus is already gone.
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

    // Unsubscribes (if the bus is still alive) and resets to the
    // moved-from/default state. Shared by the destructor and by move
    // assignment (which must drop whatever *this* held before).
    void Reset() noexcept;

    EventBus* m_bus = nullptr;
    const void* m_tag = nullptr;
    uint64_t m_id = 0;
    std::shared_ptr<std::atomic<bool>> m_busAlive;
};

// Owned by its composition root (a Workspace, in M3) — NOT a singleton.
// See the file header above for the full Post/Pump/Subscription contract.
//
// Lifetime contract: the bus must not be destroyed concurrently with a
// live Subscription's destruction on another thread. Owned by a
// composition root (Workspace), teardown is destruction order:
// subscriptions die before, or sequentially after, the bus — never
// simultaneously on another thread. The alive-flag makes sequential
// outliving a safe no-op; it does not synchronize concurrent
// destruction.
class EventBus {
public:
    EventBus();
    ~EventBus();

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    // Per-event-type identity without RTTI: `tag` is unique per
    // instantiation of TypeTag<E> and stable for the program's lifetime.
    template <class E>
    static const void* TypeTag() {
        static const int tag = 0;
        return &tag;
    }

    // Subscribes `fn` to events of type E. The returned Subscription owns
    // the registration: destroying it (or letting it go out of scope)
    // unsubscribes `fn`. `fn` is only ever invoked from Pump().
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

    // Queues a copy of `ev` for dispatch by a future Pump(). Safe to call
    // from any thread, including concurrently with Pump() or other
    // Post() calls. Never invokes a handler.
    template <class E>
    void Post(E ev) {
        auto data = std::make_shared<E>(std::move(ev));
        const void* tag = TypeTag<E>();
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(Detail::QueuedEvent{tag, std::move(data)});
    }

    // Dispatches every event queued as of the moment Pump() is called, in
    // FIFO order, on the calling thread. See the file header for the
    // re-entrant-Post note. The handler list for each queued event is
    // snapshotted individually, right before that event is dispatched:
    // if a handler unsubscribes another handler mid-batch, that removal
    // takes effect from the next queued event onward — the event already
    // snapshotted (the one whose handlers are currently being invoked)
    // still reaches the handler being removed. Defined out-of-line in
    // EventBus.cpp (it is not a template).
    void Pump();

    // Number of events queued and not yet drained by a Pump() call.
    // Defined out-of-line in EventBus.cpp (it is not a template).
    size_t PendingEvents() const;

private:
    friend class Subscription;

    // Removes the handler `id` registered under `tag`, if the bus still
    // has one. Called only from a live Subscription's destructor/reset.
    void Unsubscribe(const void* tag, uint64_t id);

    mutable std::mutex m_mutex;
    uint64_t m_nextId = 1;
    std::map<const void*, std::vector<Detail::EventHandlerEntry>> m_handlers;
    std::deque<Detail::QueuedEvent> m_queue;

    // Shared with every Subscription this bus has handed out. Starts
    // true; the destructor flips it to false so a Subscription outliving
    // this EventBus can detect that and skip touching `this`.
    std::shared_ptr<std::atomic<bool>> m_alive;
};

} // namespace Onyx::Services
