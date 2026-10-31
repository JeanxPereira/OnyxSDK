#include <doctest/doctest.h>

#include "Support/Harness.h"
#include "Support/PrismFixture.h"

#include <Onyx/TestKit/Goldens.h>

#include <filesystem>
#include <string>

using namespace Onyx;
using Tests::NewWorkspace;
using Tests::SamplePrism;
using Tests::TempFile;

TEST_CASE("the sample tree matches its golden") {
    TempFile file("golden.prm", SamplePrism());
    auto ws = NewWorkspace();
    const Modules::DocumentId id = ws->Open(file.path);
    REQUIRE(id != 0);
    Modules::Document* doc = ws->Get(id);
    REQUIRE(doc);

    const std::string snapshot = TestKit::SnapshotTree(*doc, ws->Catalog());
    const std::filesystem::path golden = std::filesystem::path(ONYX_GOLDEN_DIR) / "prm" / "sample.golden.json";
    TestKit::UpdateGolden(snapshot, golden);
    std::string diff;
    CHECK_MESSAGE(TestKit::CompareTreeGolden(snapshot, golden, diff), diff);
}
