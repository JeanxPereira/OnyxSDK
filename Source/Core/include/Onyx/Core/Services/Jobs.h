#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Onyx::Services {

// Progress of one running job. Step() and CancelRequested() are called by the
// Work callback on its worker; Peek() is safe from any thread.
class Progress {
public:
    struct Snapshot {
        float fraction = 0.0f;
        std::string label;
    };

    // Clamps fraction to [0, 1].
    void Step(float fraction, std::string_view label);
    bool CancelRequested() const;
    Snapshot Peek() const;

private:
    friend class JobHandle;
    void RequestCancel();

    mutable std::mutex m_mutex;
    float m_fraction = 0.0f;
    std::string m_label;
    std::atomic<bool> m_cancelRequested{false};
};

namespace Detail { struct JobState; }

// Copies share the job's state. The destructor never blocks and never cancels.
class JobHandle {
public:
    JobHandle() = default;

    // Cooperative: sets a flag the Work callback may poll via
    // Progress::CancelRequested(). Does not guarantee the job stops.
    void Cancel();

    // True once Work has returned, whether or not Pump() ran Done yet.
    bool Done() const;

    bool Valid() const { return static_cast<bool>(m_state); }
    Progress::Snapshot Peek() const;

private:
    friend class JobQueue;
    explicit JobHandle(std::shared_ptr<Detail::JobState> state)
        : m_state(std::move(state)) {}

    std::shared_ptr<Detail::JobState> m_state;
};

// Lane-serialized worker pool: at most one job per lane runs at a time, jobs of
// one lane run FIFO, different lanes run concurrently up to the worker count.
// Done callbacks run only inside Pump(), on the calling thread.
class JobQueue {
public:
    explicit JobQueue(unsigned workers = 2);

    // Blocking teardown, owner thread only. Jobs not yet started never run and
    // their Done never fires; their Work closures are released. A running job
    // finishes first, so any JobHandle still held reports Done().
    ~JobQueue();

    JobQueue(const JobQueue&) = delete;
    JobQueue& operator=(const JobQueue&) = delete;

    using Work = std::function<void(Progress&)>;
    using Done = std::function<void()>;

    JobHandle Submit(uint64_t lane, Work work, Done onDone = {});

    void Pump();

    // Finished jobs awaiting a Pump() to run their Done.
    size_t PendingCallbacks() const;

private:
    void WorkerLoop();

    // Both require m_mutex held.
    bool HasRunnableLocked() const;
    std::shared_ptr<Detail::JobState> PopRunnableLocked(uint64_t& outLane);

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stop = false;

    std::unordered_map<uint64_t, std::deque<std::shared_ptr<Detail::JobState>>> m_pending;
    std::unordered_set<uint64_t> m_activeLanes;
    std::vector<std::shared_ptr<Detail::JobState>> m_finished;

    std::vector<std::thread> m_workers;
};

}
