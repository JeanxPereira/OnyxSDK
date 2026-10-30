#include <Onyx/Core/Services/Jobs.h>

#include <Onyx/Core/Services/Log.h>

#include <algorithm>
#include <utility>

namespace Onyx::Services {

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

namespace Detail {

// `work` and `onDone` are touched by the running worker and later by the thread
// that calls Pump(); JobQueue::m_mutex orders that hand-off. `progress` and
// `done` are internally synchronized.
struct JobState {
    uint64_t lane = 0;
    std::function<void(Progress&)> work;
    std::function<void()> onDone;
    Progress progress;
    std::atomic<bool> done{false};
};

}

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

    // Workers are gone: release the closures of jobs that never started so
    // their captures are not pinned inside orphaned states.
    for (auto& [lane, dq] : m_pending) {
        for (auto& job : dq) {
            job->work = nullptr;
        }
    }
    m_pending.clear();
    m_finished.clear();
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
            // Stop wins over a runnable job: unstarted jobs never run once
            // teardown has begun.
            if (m_stop) {
                return;
            }
            job = PopRunnableLocked(lane);
            if (!job) {
                continue;
            }
        }

        // `job` stays referenced by this worker until Done is queued below, so
        // the state cannot expire under a JobHandle during teardown.
        try {
            job->work(job->progress);
        } catch (...) {
            ONYX_LOG_ERROR("Jobs", "job on lane {} threw; treated as failed", lane);
        }
        // Drop Work's captures: a captured object may hold this job's handle,
        // which would otherwise form an ownership cycle.
        job->work = nullptr;
        job->done.store(true, std::memory_order_release);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_activeLanes.erase(lane);
            auto it = m_pending.find(lane);
            if (it != m_pending.end() && it->second.empty()) {
                m_pending.erase(it);
            }
            m_finished.push_back(std::move(job));
        }
        m_cv.notify_all();
    }
}

void JobQueue::Pump() {
    std::vector<std::shared_ptr<Detail::JobState>> finished;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        finished.swap(m_finished);
    }
    for (auto& job : finished) {
        if (job->onDone) {
            try {
                job->onDone();
            } catch (...) {
                ONYX_LOG_ERROR("Jobs", "Done callback on lane {} threw; continuing drain", job->lane);
            }
        }
    }
}

size_t JobQueue::PendingCallbacks() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_finished.size();
}

}
