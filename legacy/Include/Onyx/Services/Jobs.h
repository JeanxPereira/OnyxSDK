#pragma once

// JobQueue — lane-serialized worker pool with cooperative cancellation.
//
// Lane = future DocumentId. At most one job of a given lane runs at any
// moment; jobs of the same lane run FIFO. Jobs of different lanes run
// concurrently, up to the worker count.
//
// JobHandle's destructor detaches: it never blocks and never cancels the
// job — it just drops a shared_ptr. Cancel() is an explicit cooperative
// flag; the Work callback must poll it via Progress::CancelRequested()
// and return on its own.
//
// Completion callbacks (Done) run only inside Pump(), on the thread that
// calls it — never on a worker thread.
//
// JobQueue::~JobQueue() signals stop and joins its worker threads
// (documented blocking teardown; owner-only). Jobs still queued and never
// started when the queue is destroyed never run, so their Done callbacks
// never fire. Jobs already mid-flight are allowed to finish; the
// destructor waits for them (their Done callbacks still won't run, since
// Done only ever fires from Pump() and nothing pumps after destruction).
// Once every worker has joined, the destructor also drains whatever is
// still sitting in the pending queue and clears each of those jobs' Work
// closures, so anything they captured (e.g. a shared_ptr<Document>)
// releases instead of leaking forever inside an orphaned JobState — this
// mirrors WorkerLoop's own `job->work = nullptr` after a job actually
// runs. Their Done callbacks still never fire; that contract is
// unchanged.

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

// Progress reporting for one running job. The Work callback (worker
// thread) writes via Step()/is polled via CancelRequested(); any other
// thread reads a consistent snapshot via Peek(). One instance lives per
// job, owned by the job's shared internal state, so it outlives Submit().
class Progress {
public:
    struct Snapshot {
        float fraction = 0.0f;
        std::string label;
    };

    // Producer side (worker thread). Clamps fraction to [0, 1].
    void Step(float fraction, std::string_view label);

    // Consumer side (any thread): has Cancel() been requested?
    bool CancelRequested() const;

    // Consumer side (any thread): a consistent (fraction, label) pair.
    Snapshot Peek() const;

private:
    friend class JobHandle;
    void RequestCancel();

    mutable std::mutex m_mutex;      // guards m_fraction + m_label together
    float m_fraction = 0.0f;
    std::string m_label;
    std::atomic<bool> m_cancelRequested{false};
};

namespace Detail { struct JobState; }

// Value handle to a submitted job. Copies share the same underlying job
// state. The destructor never blocks and never cancels — it only drops a
// reference to that state.
class JobHandle {
public:
    JobHandle() = default;

    // Cooperative: sets a flag the job's Work callback may observe via
    // Progress::CancelRequested(). Never blocks; does not guarantee the
    // job stops.
    void Cancel();

    // True once the job's Work callback has returned (independent of
    // whether Pump() has run its Done callback yet).
    bool Done() const;

    bool Valid() const { return static_cast<bool>(m_state); }

    // Snapshot of the job's progress as of the last Step() call.
    Progress::Snapshot Peek() const;

private:
    friend class JobQueue;
    explicit JobHandle(std::shared_ptr<Detail::JobState> state)
        : m_state(std::move(state)) {}

    std::shared_ptr<Detail::JobState> m_state;
};

// Worker pool with per-lane serialization: at most one job of a given
// lane runs at a time (lane = future DocumentId). Jobs submitted to the
// same lane run FIFO; jobs of different lanes run concurrently, up to
// `workers` threads.
//
// Completion callbacks (Done) run only inside Pump(), on the thread that
// calls it.
class JobQueue {
public:
    explicit JobQueue(unsigned workers = 2);

    // Signals stop and joins all worker threads, then drains every job
    // still sitting in the pending queue (never started) and clears each
    // one's Work closure so whatever it captured releases instead of
    // leaking forever pinned inside an orphaned JobState. Documented
    // blocking teardown — call only from the owning thread. Jobs not yet
    // started never run and their Done callback never fires (Done only
    // ever runs from Pump(), and nothing pumps after destruction); a job
    // already running is allowed to finish before its worker exits.
    ~JobQueue();

    JobQueue(const JobQueue&) = delete;
    JobQueue& operator=(const JobQueue&) = delete;

    using Work = std::function<void(Progress&)>;
    using Done = std::function<void()>;

    JobHandle Submit(uint64_t lane, Work work, Done onDone = {});

    // Invokes the Done callback of every job that finished since the last
    // Pump(), on the calling thread. Never invoked from a worker thread.
    void Pump();

    // Number of finished jobs awaiting a Pump() to run their Done.
    size_t PendingCallbacks() const;

private:
    void WorkerLoop();

    // Both require m_mutex to already be held by the caller.
    bool HasRunnableLocked() const;
    std::shared_ptr<Detail::JobState> PopRunnableLocked(uint64_t& outLane);

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stop = false;

    // lane -> FIFO queue of not-yet-started jobs for that lane.
    std::unordered_map<uint64_t, std::deque<std::shared_ptr<Detail::JobState>>> m_pending;
    // Lanes with a job currently running on some worker.
    std::unordered_set<uint64_t> m_activeLanes;
    // Jobs whose Work finished and are awaiting Pump().
    std::vector<std::shared_ptr<Detail::JobState>> m_finished;

    std::vector<std::thread> m_workers;
};

} // namespace Onyx::Services
