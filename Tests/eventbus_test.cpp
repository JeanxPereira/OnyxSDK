#include <doctest/doctest.h>

#include <Onyx/Services/EventBus.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace {
struct EvA { int v; };
struct EvB { std::string s; };
} // namespace

TEST_CASE("EventBus dispatches FIFO on Pump, not on Post") {
    Onyx::Services::EventBus bus;
    std::vector<int> got;
    auto sub = bus.On<EvA>([&](const EvA& e){ got.push_back(e.v); });
    bus.Post(EvA{1}); bus.Post(EvA{2});
    CHECK(got.empty());                   // queued, not immediate
    bus.Pump();
    CHECK(got == std::vector<int>{1, 2});
}

TEST_CASE("Subscription RAII: a dead subscriber is never called") {
    Onyx::Services::EventBus bus;
    int calls = 0;
    { auto sub = bus.On<EvA>([&](const EvA&){ ++calls; }); }
    bus.Post(EvA{1});
    bus.Pump();
    CHECK(calls == 0);
}

TEST_CASE("Distinct event types do not cross") {
    Onyx::Services::EventBus bus;
    int a = 0, b = 0;
    auto sa = bus.On<EvA>([&](const EvA&){ ++a; });
    auto sb = bus.On<EvB>([&](const EvB&){ ++b; });
    bus.Post(EvB{"x"});
    bus.Pump();
    CHECK(a == 0); CHECK(b == 1);
}

TEST_CASE("Cross-thread Post is safe") {
    Onyx::Services::EventBus bus;
    std::atomic<int> got{0};
    auto sub = bus.On<EvA>([&](const EvA&){ ++got; });
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t)
        ts.emplace_back([&]{ for (int i = 0; i < 250; ++i) bus.Post(EvA{i}); });
    for (auto& t : ts) t.join();
    bus.Pump();
    CHECK(got == 1000);
}

// Not in the brief's test block, but required by step 3's prose: a
// Subscription that outlives its EventBus must be a safe no-op, never a
// dangling-pointer dereference.
TEST_CASE("Subscription outliving its EventBus is a safe no-op") {
    int calls = 0;
    Onyx::Services::Subscription sub;
    {
        Onyx::Services::EventBus bus;
        sub = bus.On<EvA>([&](const EvA&){ ++calls; });
        bus.Post(EvA{1});
        bus.Pump();
    }
    // `bus` is gone now; destroying `sub` below must not touch it.
    CHECK(calls == 1);
}
