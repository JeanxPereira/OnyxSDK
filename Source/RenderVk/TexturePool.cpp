#include <Onyx/RenderVk/TexturePool.h>

#include <algorithm>

namespace Onyx::RenderVk {

void DeferredDestroyQueue::Retire(uint64_t retiredFrame, std::function<void()> destroy) {
    m_entries.push_back(Entry{retiredFrame, std::move(destroy)});
}

void DeferredDestroyQueue::Collect(uint64_t currentFrame, uint32_t framesInFlight) {
    // Partition rather than erase-while-iterating: entries that fire keep
    // their original relative order (Retire()'s own ordering guarantee),
    // and only one pass over the vector is needed either way.
    auto firstSafe = std::stable_partition(
        m_entries.begin(), m_entries.end(), [&](const Entry& e) {
            // NOT safe yet (kept in the front partition) when the frame
            // distance hasn't elapsed. Written as a subtraction-free
            // comparison so an unsigned currentFrame < retiredFrame
            // (should not happen in practice, but costs nothing to guard)
            // never wraps into a huge distance and fires early.
            return currentFrame < e.retiredFrame ||
                   (currentFrame - e.retiredFrame) < framesInFlight;
        });

    for (auto it = firstSafe; it != m_entries.end(); ++it) {
        if (it->destroy) it->destroy();
    }
    m_entries.erase(firstSafe, m_entries.end());
}

void DeferredDestroyQueue::CollectAll() {
    for (Entry& e : m_entries) {
        if (e.destroy) e.destroy();
    }
    m_entries.clear();
}

} // namespace Onyx::RenderVk
