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
