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
    // Iterate a plain index rather than a range-for/cached iterator over
    // m_passes: that only protects the TRAVERSAL itself (advancing past a
    // vector that reallocated or shrank underneath it), not the elements
    // it reads. Reentrant AddPass()/RemovePass() calls from inside a pass
    // (e.g. a closure that captured this RenderContext) are UNSUPPORTED
    // this milestone -- a reentrant AddPass may reallocate m_passes mid-
    // loop, and a reentrant RemovePass may compact it out from under this
    // loop, silently skipping or deferring passes within the current frame.
    // Neither corrupts state (m_passes itself stays internally consistent)
    // but the exact set of passes that run this frame is not guaranteed
    // once a pass mutates the registry it is being invoked from.
    for (size_t i = 0; i < m_passes.size(); ++i) {
        // Copy the name BEFORE invoking, not after: `pass.fn` may itself
        // (unsupportedly, per above) call RemovePass and erase this very
        // element, which would leave a `const Pass&` taken before the call
        // dangling by the time a catch arm below tries to log pass.name.
        // A local std::string copy has no such lifetime dependency on
        // m_passes staying stable across the call.
        const std::string passName = m_passes[i].name;
        try {
            m_passes[i].fn(handles);
        } catch (const std::exception& e) {
            // §7.1: contained, not propagated -- the pass is skipped for
            // THIS frame only (never removed; see AddPass's doc comment on
            // why a transient throw must not silently unregister a pass).
            // "Skipped" is about what happens after the throw: any command
            // the pass already recorded into handles.cmd before throwing
            // is NOT rolled back (see RenderContext.h's class doc comment).
            LOG_WARN("[RenderContext] pass '%s' threw and was skipped this frame: %s",
                      passName.c_str(), e.what());
        } catch (...) {
            LOG_WARN("[RenderContext] pass '%s' threw (non-std::exception) and was skipped "
                      "this frame", passName.c_str());
        }
    }
}

} // namespace Onyx::RenderVk
