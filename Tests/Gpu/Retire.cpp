#include <doctest/doctest.h>

#include <vector>

#include "Onyx/Gpu/Retire.h"

using namespace Onyx;

TEST_CASE("RetireQueue runs deleters in FIFO order once their value completes") {
    Gpu::RetireQueue q;
    std::vector<int> ran;
    q.Enqueue([&] { ran.push_back(1); }, 3);
    q.Enqueue([&] { ran.push_back(2); }, 1);
    q.Enqueue([&] { ran.push_back(3); }, 3);
    q.Collect(2);
    CHECK(ran == std::vector<int>{2});
    CHECK(q.Pending() == 2);
    q.Collect(3);
    CHECK(ran == std::vector<int>{2, 1, 3});
    CHECK(q.Pending() == 0);
}

TEST_CASE("RetireQueue::Drain runs everything regardless of value") {
    Gpu::RetireQueue q;
    int ran = 0;
    q.Enqueue([&] { ++ran; }, 10);
    q.Enqueue([&] { ++ran; }, 20);
    q.Drain();
    CHECK(ran == 2);
    CHECK(q.Pending() == 0);
}

TEST_CASE("RetireQueue is safe to Enqueue from a deleter") {
    Gpu::RetireQueue q;
    std::vector<int> ran;
    q.Enqueue([&] {
        ran.push_back(1);
        q.Enqueue([&] { ran.push_back(2); }, 5);
    }, 5);
    q.Collect(5);
    CHECK(ran == std::vector<int>{1});
    CHECK(q.Pending() == 1);
    q.Collect(5);
    CHECK(ran == std::vector<int>{1, 2});
    CHECK(q.Pending() == 0);
}
