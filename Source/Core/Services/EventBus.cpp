#include <Onyx/Core/Services/EventBus.h>

#include <Onyx/Core/Services/Log.h>

#include <algorithm>

namespace Onyx::Services {

EventBus::EventBus() : m_alive(std::make_shared<std::atomic<bool>>(true)) {}

EventBus::~EventBus() {
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
    std::deque<Detail::QueuedEvent> queue;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        queue.swap(m_queue);
    }

    for (auto& queued : queue) {
        // Only ids are snapshotted; each handler is re-looked-up right before
        // it runs so an earlier handler can unsubscribe a later one.
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
                    break;
                auto hit = std::find_if(it->second.begin(), it->second.end(),
                                         [id](const Detail::EventHandlerEntry& e) { return e.id == id; });
                if (hit == it->second.end())
                    continue;
                invoke = hit->invoke;
            }

            try {
                invoke(queued.data.get());
            } catch (...) {
                ONYX_LOG_ERROR("EventBus", "handler threw during Pump; continuing");
            }
        }
    }
}

size_t EventBus::PendingEvents() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_queue.size();
}

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
        Reset();
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

}
