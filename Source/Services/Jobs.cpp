#include <Onyx/Services/Jobs.h>

#include <Onyx/Services/Logger.h>

#include <algorithm>
#include <utility>

namespace Onyx::Services {

// ── Progress ────────────────────────────────────────────────────────────

void Progress::Step(float fraction, std::string_view label) {
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    std::lock_guard<std::mutex> lock(m_mutex);
    m_fraction = fraction;
    m_label.assign(label);
}

bool Progress::CancelRequested() const {
    return m_cancelRequested.load(std::memory_order_relaxed);
}

Progress::Snapshot Progress::Peek() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return Snapshot{m_fraction, m_label};
}

void Progress::RequestCancel() {
    m_cancelRequested.store(true, std::memory_order_relaxed);
}

// ── Detail::JobState ───────────────────────────────────────────────────

namespace Detail {

// Shared state for one submitted job. Ownership crosses threads via
// shared_ptr; the fields themselves never cross without synchronization:
// `work`/`onDone` are only ever touched by the single worker that runs
// the job and, for onDone, by whichever thread later calls Pump() — the
// hand-off between those two is ordered by JobQueue::m_mutex (the worker
// locks it to publish into m_finished; Pump() locks it to drain
// m_finished), so no separate synchronization is needed on the functions
// themselves. `progress` and `done` are the only fields touched
// concurrently from arbitrary threads (JobHandle::Peek/Cancel/Done vs.
// the worker), and both are internally synchronized (mutex / atomic).
struct JobState {
    uint64_t lane = 0;
    std::function<void(Progress&)> work;
    std::function<void()> onDone;
    Progress progress;
    std::atomic<bool> done{false};
};

} // namespace Detail

// ── JobHandle ───────────────────────────────────────────────────────────

void JobHandle::Cancel() {
    if (m_state) {
        m_state->progress.RequestCancel();
    }
}

bool JobHandle::Done() const {
    return m_state && m_state->done.load(std::memory_order_acquire);
}

Progress::Snapshot JobHandle::Peek() const {
    if (!m_state) {
        return {};
    }
    return m_state->progress.Peek();
}

// ── JobQueue ────────────────────────────────────────────────────────────

JobQueue::JobQueue(unsigned workers) {
    if (workers == 0) {
        workers = 1;
    }
    m_workers.reserve(workers);
    for (unsigned i = 0; i < workers; ++i) {
        m_workers.emplace_back([this] { WorkerLoop(); });
    }
}

JobQueue::~JobQueue() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    for (auto& t : m_workers) {
        if (t.joinable()) {
            t.join();
        }
    }
}

JobHandle JobQueue::Submit(uint64_t lane, Work work, Done onDone) {
    auto state = std::make_shared<Detail::JobState>();
    state->lane = lane;
    state->work = std::move(work);
    state->onDone = std::move(onDone);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending[lane].push_back(state);
    }
    m_cv.notify_one();
    return JobHandle(state);
}

bool JobQueue::HasRunnableLocked() const {
    for (const auto& [lane, dq] : m_pending) {
        if (!dq.empty() && m_activeLanes.find(lane) == m_activeLanes.end()) {
            return true;
        }
    }
    return false;
}

std::shared_ptr<Detail::JobState> JobQueue::PopRunnableLocked(uint64_t& outLane) {
    for (auto& [lane, dq] : m_pending) {
        if (!dq.empty() && m_activeLanes.find(lane) == m_activeLanes.end()) {
            auto job = dq.front();
            dq.pop_front();
            m_activeLanes.insert(lane);
            outLane = lane;
            return job;
        }
    }
    return nullptr;
}

void JobQueue::WorkerLoop() {
    for (;;) {
        std::shared_ptr<Detail::JobState> job;
        uint64_t lane = 0;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_stop || HasRunnableLocked(); });
            // Stop wins even if a runnable job also exists: queued-but-
            // unstarted jobs must not run once teardown has begun.
            if (m_stop) {
                return;
            }
            job = PopRunnableLocked(lane);
            if (!job) {
                // Spurious: another worker claimed the only runnable lane
                // between the predicate check and our pop. Loop again.
                continue;
            }
        }

        // Run Work with no lock held — this is the whole point of the
        // pool, and holding the queue mutex here would serialize every
        // lane against every other one. Work is arbitrary user code;
        // exceptions never cross a service boundary in this SDK, so a
        // throw is contained here and the job still completes normally
        // (done flag set, lane released, Done still queued for Pump) —
        // a failed job must never wedge its lane or take down the
        // process.
        try {
            job->work(job->progress);
        } catch (...) {
            LOG_ERR("[Jobs] job on lane %llu threw; treated as failed",
                    (unsigned long long)lane);
        }
        job->done.store(true, std::memory_order_release);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_activeLanes.erase(lane);
            auto it = m_pending.find(lane);
            if (it != m_pending.end() && it->second.empty()) {
                m_pending.erase(it);
            }
            m_finished.push_back(job);
        }
        // Freeing the lane may let a queued job in it (or a job that was
        // blocked behind nothing but was simply not yet scanned) become
        // runnable for another idle worker.
        m_cv.notify_all();
    }
}

void JobQueue::Pump() {
    std::vector<std::shared_ptr<Detail::JobState>> finished;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        finished.swap(m_finished);
    }
    // Invoke Done with no lock held: user code must never run while the
    // queue mutex is taken.
    for (auto& job : finished) {
        if (job->onDone) {
            // A throwing Done must not skip the remaining drained
            // callbacks -- same containment policy as a throwing Work.
            try {
                job->onDone();
            } catch (...) {
                LOG_ERR("[Jobs] Done callback on lane %llu threw; continuing drain",
                        (unsigned long long)job->lane);
            }
        }
    }
}

size_t JobQueue::PendingCallbacks() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_finished.size();
}

} // namespace Onyx::Services
