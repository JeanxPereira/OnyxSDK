#include <doctest/doctest.h>
#include <thread>
#include <vector>

#include <Onyx/Services/Diagnostics.h>

TEST_CASE("DiagSink collects, counts and drains") {
    Onyx::Services::DiagSink sink;
    sink.Report({Onyx::Services::Severity::Warning, "t.w", "warn", {}});
    sink.Report({Onyx::Services::Severity::Error,   "t.e", "err",
                 Onyx::Services::ByteRef{"a.wad", 0x40}});
    CHECK(sink.Count() == 2);
    CHECK(sink.Count(Onyx::Services::Severity::Error) == 1);
    CHECK(sink.HasErrors());
    auto out = sink.Drain();
    REQUIRE(out.size() == 2);
    CHECK(out[1].at->offset == 0x40);
    CHECK(sink.Count() == 0);
}

TEST_CASE("DiagSink Snapshot copies without clearing") {
    Onyx::Services::DiagSink sink;
    sink.Report({Onyx::Services::Severity::Warning, "t.w", "warn about foo", {}});
    sink.Report({Onyx::Services::Severity::Error,   "t.e", "err about bar", {}});

    auto first = sink.Snapshot();
    REQUIRE(first.size() == 2);
    CHECK(first[0].message == "warn about foo");

    // Unlike Drain(), a Snapshot() must leave the sink untouched: a second
    // Snapshot() (or a Count()/HasErrors() from an unrelated reader) still
    // sees everything.
    CHECK(sink.Count() == 2);
    auto second = sink.Snapshot();
    CHECK(second.size() == 2);
}

TEST_CASE("DiagSink is safe under concurrent producers") {
    Onyx::Services::DiagSink sink;
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t)
        ts.emplace_back([&sink]{
            for (int i = 0; i < 1000; ++i)
                sink.Report({Onyx::Services::Severity::Info, "t.i", "x", {}});
        });
    for (auto& t : ts) t.join();
    CHECK(sink.Count() == 4000);
}
