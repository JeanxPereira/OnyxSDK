#include <Onyx/RenderVk/RenderContext.h>

#include <Onyx/Services/Logger.h>

#include <algorithm>
#include <exception>

namespace Onyx::RenderVk {

int RenderContext::AddPass(std::string name, std::function<void(const FrameHandles&)> fn) {
    const int id = m_nextId++;
    m_passes.push_back(Pass{id, std::move(name), std::move(fn)});
    return id;
}

void RenderContext::RemovePass(int id) {
    m_passes.erase(std::remove_if(m_passes.begin(), m_passes.end(),
                                   [id](const Pass& p) { return p.id == id; }),
                   m_passes.end());
}

void RenderContext::Execute(const FrameHandles& handles) {
    // Iterate a plain index, not a range-for over m_passes: a pass's own
    // callback has no way to reach this RenderContext (it only receives
    // `handles`), so m_passes cannot be mutated mid-loop today -- but
    // indexing rather than caching an iterator/reference keeps this loop
    // correct even if a future caller's `fn` closure captured the
    // RenderContext and called AddPass/RemovePass on it from inside a pass.
    for (size_t i = 0; i < m_passes.size(); ++i) {
        const Pass& pass = m_passes[i];
        try {
            pass.fn(handles);
        } catch (const std::exception& e) {
            // §7.1: contained, not propagated -- the pass is skipped for
            // THIS frame only (never removed; see AddPass's doc comment on
            // why a transient throw must not silently unregister a pass).
            LOG_WARN("[RenderContext] pass '%s' threw and was skipped this frame: %s",
                      pass.name.c_str(), e.what());
        } catch (...) {
            LOG_WARN("[RenderContext] pass '%s' threw (non-std::exception) and was skipped "
                      "this frame", pass.name.c_str());
        }
    }
}

} // namespace Onyx::RenderVk
