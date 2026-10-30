#include <doctest/doctest.h>

#include <Onyx/Core/Services/Threading.h>

#include <thread>

using namespace Onyx::Services;

TEST_CASE("Threading.MainThreadIsMarkedOnce") {
    MarkMainThread();
    CHECK(IsMainThread());
    MarkMainThread();
    CHECK(IsMainThread());
}

TEST_CASE("Threading.WorkerIsNotMain") {
    MarkMainThread();
    bool workerIsMain = true;
    std::thread t([&] { workerIsMain = IsMainThread(); });
    t.join();
    CHECK_FALSE(workerIsMain);
    CHECK(IsMainThread());
}
