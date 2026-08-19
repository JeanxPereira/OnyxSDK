#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include <Onyx/Services/Jobs.h>

TEST_CASE("JobQueue serializes jobs within a lane") {
    Onyx::Services::JobQueue q(4);
    std::vector<int> order;
    std::mutex mx;
    std::atomic<int> pending{3};
    for (int i = 0; i < 3; ++i) {
        q.Submit(7, [&, i](Onyx::Services::Progress&) {
            { std::lock_guard<std::mutex> l(mx); order.push_back(i); }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            --pending;
        });
    }
    while (pending > 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(order == std::vector<int>{0, 1, 2});   // same lane => FIFO
}

TEST_CASE("Cancellation is cooperative and Done callbacks only fire in Pump") {
    Onyx::Services::JobQueue q(1);
    std::atomic<bool> sawCancel{false}, doneRan{false};
    auto h = q.Submit(1, [&](Onyx::Services::Progress& p) {
        while (!p.CancelRequested())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        sawCancel = true;
    }, [&]{ doneRan = true; });
    h.Cancel();
    while (!sawCancel) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    while (q.PendingCallbacks() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK_FALSE(doneRan);                 // not yet: no Pump ran
    q.Pump();
    CHECK(doneRan);
}

TEST_CASE("Progress snapshots are consistent") {
    Onyx::Services::JobQueue q(1);
    std::atomic<bool> go{false}, quit{false};
    auto h = q.Submit(2, [&](Onyx::Services::Progress& p) {
        p.Step(0.5f, "halfway");
        go = true;
        while (!quit) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    while (!go) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    // Peek through the handle's state, which delegates to the job's
    // Progress. Must observe the (fraction, label) pair set together by
    // the one Step() call above -- not a torn mix of an old fraction with
    // a new label or vice versa.
    auto snap = h.Peek();
    CHECK(snap.fraction == doctest::Approx(0.5f));
    CHECK(snap.label == "halfway");
    quit = true;
}

TEST_CASE("A throwing Work neither crashes nor wedges its lane") {
    Onyx::Services::JobQueue q(1);
    std::atomic<bool> doneRan{false}, secondRan{false};
    q.Submit(9, [](Onyx::Services::Progress&) { throw std::runtime_error("boom"); },
             [&]{ doneRan = true; });
    q.Submit(9, [&](Onyx::Services::Progress&) { secondRan = true; });
    while (!secondRan) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    while (q.PendingCallbacks() < 1) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    q.Pump();
    CHECK(doneRan);       // failed job still completes through Pump
    CHECK(secondRan);     // lane was released, next job ran
}

TEST_CASE("JobQueue Pump contains a throwing Done callback so later Done callbacks still run") {
    Onyx::Services::JobQueue q(2);
    std::atomic<bool> secondDoneRan{false};
    q.Submit(30, [](Onyx::Services::Progress&) {},
             []{ throw std::runtime_error("boom"); });
    q.Submit(31, [](Onyx::Services::Progress&) {},
             [&]{ secondDoneRan = true; });
    while (q.PendingCallbacks() < 2) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    q.Pump();
    CHECK(secondDoneRan);   // the first Done's throw must not skip the second
}

TEST_CASE("JobQueue destruction drops queued-but-unstarted jobs on the same lane") {
    std::atomic<bool> firstRunning{false}, releaseFirst{false};
    std::atomic<bool> secondWorkRan{false}, secondDoneRan{false};
    {
        Onyx::Services::JobQueue q(1); // single worker: job 2 cannot start
                                        // while job 1 is still mid-flight
        q.Submit(42, [&](Onyx::Services::Progress&) {
            firstRunning = true;
            while (!releaseFirst)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });
        q.Submit(42, [&](Onyx::Services::Progress&) { secondWorkRan = true; },
                     [&]{ secondDoneRan = true; });
        while (!firstRunning) std::this_thread::sleep_for(std::chrono::milliseconds(1));

        // Destroy the queue while job 1 is mid-flight; job 2 is still
        // sitting unstarted in the pending deque for lane 42.
        releaseFirst = true;
    } // ~JobQueue: signals stop, joins workers -- job 2 must never run

    CHECK_FALSE(secondWorkRan);
    CHECK_FALSE(secondDoneRan);
}

TEST_CASE("JobQueue destruction releases resources captured by unstarted jobs' Work closures") {
    std::atomic<bool> aRunning{false}, releaseA{false};
    auto shared = std::make_shared<int>(42);
    std::weak_ptr<int> weak = shared;
    {
        Onyx::Services::JobQueue q(1); // single worker: B and C can never
                                        // start while A is still mid-flight,
                                        // even though they sit on other lanes
        q.Submit(1, [&](Onyx::Services::Progress&) {
            aRunning = true;
            while (!releaseA)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });
        q.Submit(2, [](Onyx::Services::Progress&) {});          // B: never starts
        q.Submit(3, [shared](Onyx::Services::Progress&) {});    // C: captures `shared`

        while (!aRunning) std::this_thread::sleep_for(std::chrono::milliseconds(1));

        shared.reset();               // C's Work closure is the only other owner now
        CHECK_FALSE(weak.expired());  // ... so the int is still alive

        // Release A so the destructor's join can return, then destroy the
        // queue while B and C are still sitting unstarted in m_pending.
        releaseA = true;
    } // ~JobQueue: joins the worker, then drains B and C's Work closures --
      // C's captured shared_ptr<int> must release here.

    CHECK(weak.expired());
}
