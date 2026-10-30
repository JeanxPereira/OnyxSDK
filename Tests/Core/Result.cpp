#include <doctest/doctest.h>

#include <Onyx/Core/Result.h>

#include <string>

TEST_CASE("Result.OkCarriesValue") {
    auto r = Onyx::Result<int>::Ok(42);
    CHECK(r.ok());
    CHECK(static_cast<bool>(r));
    CHECK(r.value() == 42);
}

TEST_CASE("Result.FailCarriesError") {
    auto r = Onyx::Result<std::string>::Fail({"io.missing", "file not found", 2});
    CHECK_FALSE(r.ok());
    CHECK_FALSE(static_cast<bool>(r));
    CHECK(r.error().code == "io.missing");
    CHECK(r.error().message == "file not found");
    CHECK(r.error().native == 2);
}

TEST_CASE("Result.VoidOk") {
    auto ok = Onyx::Result<void>::Ok();
    CHECK(ok.ok());
    auto bad = Onyx::Result<void>::Fail({"x", "y"});
    CHECK_FALSE(bad.ok());
    CHECK(bad.error().code == "x");
    CHECK(bad.error().native == 0);
}
