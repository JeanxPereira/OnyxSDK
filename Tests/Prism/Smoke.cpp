#include <doctest/doctest.h>

#include "Support/Harness.h"
#include "Support/PrismFixture.h"

#include <Onyx/TestKit/DecodeSmoke.h>

using namespace Onyx;
using Tests::NewWorkspace;
using Tests::SamplePrism;
using Tests::TempFile;

TEST_CASE("DecodeAll decodes every slot with only bad in the allowlist") {
    TempFile file("smoke.prm", SamplePrism());
    auto ws = NewWorkspace();
    const Modules::DocumentId id = ws->Open(file.path);
    REQUIRE(id != 0);

    const TestKit::SmokeResult result = TestKit::DecodeAll(*ws, id, {"bad"});
    CHECK(result.failed == 0);
    CHECK(result.errors.empty());
    CHECK(result.decoded == 6);
}
