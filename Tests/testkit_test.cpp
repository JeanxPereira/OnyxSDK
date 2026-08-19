// ── TestKit tests (Task 1, M5) ─────────────────────────────────────────────
//
// Exercises Onyx::TestKit's three modules (Goldens, DecodeSmoke,
// RenderCompare) against the same synthetic OBX1 fixture shapes
// Tests/cli_test.cpp already established (local, adapted copies -- not
// included from there, so this file's TEST_CASE set stays independent, same
// precedent cli_test.cpp itself set against Tests/onyxbox_test.cpp).

#include <doctest/doctest.h>

#include <Onyx/Domain/Entry.h>
#include <Onyx/Modules/Workspace.h>
#include <Onyx/TestKit/DecodeSmoke.h>
#include <Onyx/TestKit/Goldens.h>
#include <Onyx/TestKit/RenderCompare.h>
#include <Onyx/Types/TypeCatalog.h>

#include <OnyxBoxModule.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

void PutU16LE(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(uint8_t(v & 0xFF));
    buf.push_back(uint8_t((v >> 8) & 0xFF));
}

void PutU32LE(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(uint8_t(v & 0xFF));
    buf.push_back(uint8_t((v >> 8) & 0xFF));
    buf.push_back(uint8_t((v >> 16) & 0xFF));
    buf.push_back(uint8_t((v >> 24) & 0xFF));
}

void PutTocHeader(std::vector<uint8_t>& buf, const std::string& name, uint8_t kind,
                   uint32_t payloadOffset, uint32_t payloadSize) {
    PutU16LE(buf, uint16_t(name.size()));
    buf.insert(buf.end(), name.begin(), name.end());
    buf.push_back(kind);
    PutU32LE(buf, payloadOffset);
    PutU32LE(buf, payloadSize);
}

// Two valid entries, no corrupt ones: "img" (kind=1, valid 8x8 RGBA) and
// "txt" (kind=2, valid "hello box") -- genuinely two entries in tree order,
// both decodable. This is the "two-entry OBX fixture" the task-1 brief
// names for both SnapshotTree's canonical-JSON test and DecodeAll's
// decoded=2/failed=0 test.
std::filesystem::path WriteTwoEntryBox() {
    std::vector<uint8_t> imagePayload;
    PutU16LE(imagePayload, 8);  // width
    PutU16LE(imagePayload, 8);  // height
    for (int i = 0; i < 256; ++i) {
        imagePayload.push_back(uint8_t(i * 7));
    }

    const std::string textPayload = "hello box";

    const uint32_t headerSize = 4 + 4 + 2 * (2 + 3 + 1 + 4 + 4); // "img"/"txt", both 3 chars
    const uint32_t imageOffset = headerSize;
    const uint32_t imageSize = uint32_t(imagePayload.size());
    const uint32_t textOffset = imageOffset + imageSize;
    const uint32_t textSize = uint32_t(textPayload.size());

    std::vector<uint8_t> buf;
    buf.insert(buf.end(), {uint8_t('O'), uint8_t('B'), uint8_t('X'), uint8_t('1')});
    PutU32LE(buf, 2);

    PutTocHeader(buf, "img", 1, imageOffset, imageSize);
    PutTocHeader(buf, "txt", 2, textOffset, textSize);

    buf.insert(buf.end(), imagePayload.begin(), imagePayload.end());
    buf.insert(buf.end(), textPayload.begin(), textPayload.end());

    auto path = std::filesystem::temp_directory_path() / "onyx_testkit_two_entry.obx";
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
    f.close();
    return path;
}

// One entry ("liar", kind=1/image) whose payload fits within the file (so
// ParseContainer never flags it Failed at parse time) but whose embedded
// width/height header claims more pixels than its declared payloadSize can
// hold -- same shape as Tests/cli_test.cpp's WriteLyingImageBox. DecodeImage
// invokes successfully (HasImage is true) but salvage-fails (returns
// nullptr, reports "obx.image.size-mismatch"), which is exactly the
// decoder-exists-but-returns-null shape DecodeAll must count as `failed`.
std::filesystem::path WriteCorruptEntryBox() {
    const uint32_t headerSize = 4 + 4 + uint32_t(2 + 4 + 1 + 4 + 4); // 1 entry, name "liar"
    const uint32_t liarOffset = headerSize;
    const uint32_t liarSize = 10; // claims 8x8 (needs 260) but only gets 10

    std::vector<uint8_t> buf;
    buf.insert(buf.end(), {uint8_t('O'), uint8_t('B'), uint8_t('X'), uint8_t('1')});
    PutU32LE(buf, 1);
    PutTocHeader(buf, "liar", 1, liarOffset, liarSize);

    PutU16LE(buf, 8); // width  -- claims 8x8 ...
    PutU16LE(buf, 8); // height -- ... which needs 260 bytes, not 10
    for (int i = 0; i < 6; ++i) buf.push_back(uint8_t(0xAA));

    auto path = std::filesystem::temp_directory_path() / "onyx_testkit_corrupt_entry.obx";
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
    f.close();
    return path;
}

// Two entries: "huge" (kind=0/blob, declared payloadSize 0xFFFFFFFF -- the
// same spec §5.4 widening shape Tests/cli_test.cpp's WriteHugeSizeBox uses)
// whose declared range vastly exceeds the file, so ParseContainer flags it
// Failed at PARSE time (not a decode-time salvage failure -- decode is
// never even attempted for it); and "txt" (kind=2, valid, in-bounds), a
// genuinely decodable sibling in the same container. Distinct from
// WriteCorruptEntryBox above: "liar" there is deliberately IN-BOUNDS at
// parse time (never flagged Failed) so its failure only surfaces once the
// decoder itself inspects the payload -- this fixture instead exercises the
// PARSE-time Failed path DecodeAll must recognize and skip outright.
std::filesystem::path WriteParseFailedSiblingBox() {
    const std::string textPayload = "hello sibling";

    const uint32_t headerSize = 4 + 4 + uint32_t(2 + 4 + 1 + 4 + 4)   // "huge"
                                       + uint32_t(2 + 3 + 1 + 4 + 4); // "txt"
    const uint32_t sharedOffset = headerSize;
    const uint32_t hugeSize = 0xFFFFFFFF; // declared -- no such bytes actually follow
    const uint32_t textSize = uint32_t(textPayload.size());

    std::vector<uint8_t> buf;
    buf.insert(buf.end(), {uint8_t('O'), uint8_t('B'), uint8_t('X'), uint8_t('1')});
    PutU32LE(buf, 2);

    PutTocHeader(buf, "huge", 0, sharedOffset, hugeSize);
    PutTocHeader(buf, "txt", 2, sharedOffset, textSize);

    buf.insert(buf.end(), textPayload.begin(), textPayload.end());

    auto path = std::filesystem::temp_directory_path() / "onyx_testkit_parse_failed_sibling.obx";
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
    f.close();
    return path;
}

std::unique_ptr<OnyxBox::OnyxBoxModule> MakeModule() {
    return std::make_unique<OnyxBox::OnyxBoxModule>();
}

} // namespace

// ── SnapshotTree / CompareTreeGolden ────────────────────────────────────────

TEST_CASE("TestKit: SnapshotTree of a two-entry OBX fixture is canonical") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());
    auto path = WriteTwoEntryBox();

    Onyx::Modules::DocumentId id = ws.Open(path);
    REQUIRE(id != 0);
    Onyx::Modules::Document* doc = ws.Get(id);
    REQUIRE(doc != nullptr);
    REQUIRE(doc->roots.size() == 2);

    std::string snapA = Onyx::TestKit::SnapshotTree(*doc);
    std::string snapB = Onyx::TestKit::SnapshotTree(*doc);

    // Same bytes on repeat (byte-stable by construction).
    CHECK(snapA == snapB);

    // Entries appear in tree order: "img" before "txt".
    size_t imgPos = snapA.find("\"img\"");
    size_t txtPos = snapA.find("\"txt\"");
    REQUIRE(imgPos != std::string::npos);
    REQUIRE(txtPos != std::string::npos);
    CHECK(imgPos < txtPos);

    // Hashes are present (a "hash" key per entry) and non-degenerate: two
    // entries with different payloads must not collide onto the same hash.
    CHECK(snapA.find("\"hash\":") != std::string::npos);
    size_t firstHash = snapA.find("\"hash\": ", imgPos);
    size_t secondHash = snapA.find("\"hash\": ", txtPos);
    REQUIRE(firstHash != std::string::npos);
    REQUIRE(secondHash != std::string::npos);
    CHECK(snapA.substr(firstHash, 24) != snapA.substr(secondHash, 24));

    // Keys and sizes are present too.
    CHECK(snapA.find("\"key\":") != std::string::npos);
    CHECK(snapA.find("\"size\":") != std::string::npos);

    ws.Close(id);
    std::filesystem::remove(path);
}

TEST_CASE("TestKit: CompareTreeGolden reports a precise diff and writes .actual on mismatch") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());
    auto path = WriteTwoEntryBox();

    Onyx::Modules::DocumentId id = ws.Open(path);
    REQUIRE(id != 0);
    Onyx::Modules::Document* doc = ws.Get(id);
    REQUIRE(doc != nullptr);

    std::string snapshot = Onyx::TestKit::SnapshotTree(*doc);

    auto goldenPath = std::filesystem::temp_directory_path() / "onyx_testkit_golden.json";
    auto actualPath = goldenPath;
    actualPath += ".actual";
    std::filesystem::remove(goldenPath);
    std::filesystem::remove(actualPath);

    // No golden file yet: mismatch, .actual written.
    {
        std::string diff;
        bool ok = Onyx::TestKit::CompareTreeGolden(snapshot, goldenPath, diff);
        CHECK_FALSE(ok);
        CHECK_FALSE(diff.empty());
        REQUIRE(std::filesystem::exists(actualPath));
    }

    // Freeze the golden, then compare identical: must pass with no diff.
    {
        std::ofstream out(goldenPath, std::ios::binary);
        out << snapshot;
    }
    std::filesystem::remove(actualPath);
    {
        std::string diff;
        bool ok = Onyx::TestKit::CompareTreeGolden(snapshot, goldenPath, diff);
        CHECK(ok);
        CHECK(diff.empty());
        CHECK_FALSE(std::filesystem::exists(actualPath));
    }

    // Mutate the snapshot (rename one entry's reported name) and compare
    // against the frozen golden: must fail, name the differing line, and
    // write the mutated snapshot as .actual.
    {
        std::string mutated = snapshot;
        size_t pos = mutated.find("\"img\"");
        REQUIRE(pos != std::string::npos);
        mutated.replace(pos, 5, "\"imx\"");

        std::string diff;
        bool ok = Onyx::TestKit::CompareTreeGolden(mutated, goldenPath, diff);
        CHECK_FALSE(ok);
        CHECK(diff.find("differs") != std::string::npos);
        REQUIRE(std::filesystem::exists(actualPath));

        std::ifstream in(actualPath, std::ios::binary);
        std::string actualContent((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(actualContent == mutated);
    }

    ws.Close(id);
    std::filesystem::remove(path);
    std::filesystem::remove(goldenPath);
    std::filesystem::remove(actualPath);
}

// ── DecodeAll ────────────────────────────────────────────────────────────

TEST_CASE("TestKit: DecodeAll on the two-entry OBX fixture returns decoded=2 failed=0") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());
    auto path = WriteTwoEntryBox();

    Onyx::Modules::DocumentId id = ws.Open(path);
    REQUIRE(id != 0);

    Onyx::TestKit::SmokeResult result = Onyx::TestKit::DecodeAll(ws, id);
    CHECK(result.decoded == 2);
    CHECK(result.failed == 0);
    CHECK(result.errors.empty());

    ws.Close(id);
    std::filesystem::remove(path);
}

TEST_CASE("TestKit: DecodeAll on the corrupt-entry fixture returns failed>=1 naming the entry") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());
    auto path = WriteCorruptEntryBox();

    Onyx::Modules::DocumentId id = ws.Open(path);
    REQUIRE(id != 0);

    Onyx::TestKit::SmokeResult result = Onyx::TestKit::DecodeAll(ws, id);
    CHECK(result.failed >= 1);
    CHECK(result.decoded == 0);
    REQUIRE_FALSE(result.errors.empty());
    bool namesLiar = false;
    for (const auto& e : result.errors) {
        if (e.find("liar") != std::string::npos) namesLiar = true;
    }
    CHECK(namesLiar);

    ws.Close(id);
    std::filesystem::remove(path);
}

TEST_CASE("TestKit: DecodeAll honors an allowlist for a known-failing entry") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());
    auto path = WriteCorruptEntryBox();

    Onyx::Modules::DocumentId id = ws.Open(path);
    REQUIRE(id != 0);

    Onyx::TestKit::SmokeResult result = Onyx::TestKit::DecodeAll(ws, id, {"liar"});
    CHECK(result.failed == 0);
    CHECK(result.skipped >= 1);
    CHECK(result.errors.empty());

    ws.Close(id);
    std::filesystem::remove(path);
}

TEST_CASE("TestKit: DecodeAll skips a parse-time Failed entry as skippedFailed, not failed") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());
    auto path = WriteParseFailedSiblingBox();

    Onyx::Modules::DocumentId id = ws.Open(path);
    REQUIRE(id != 0);
    Onyx::Modules::Document* doc = ws.Get(id);
    REQUIRE(doc != nullptr);
    REQUIRE(doc->roots.size() == 2);
    // Sanity: ParseContainer really did flag "huge" Failed (not this
    // fixture accidentally exercising some other path).
    const Onyx::Domain::AssetEntry& huge = doc->roots[0];
    REQUIRE(huge.name == "huge");
    CHECK((static_cast<uint8_t>(huge.flags) & static_cast<uint8_t>(Onyx::Domain::NodeFlags::Failed)) !=
          0);

    Onyx::TestKit::SmokeResult result = Onyx::TestKit::DecodeAll(ws, id);
    CHECK(result.skippedFailed == 1);
    CHECK(result.failed == 0);
    CHECK(result.decoded == 1); // the "txt" sibling still decodes
    CHECK(result.errors.empty());
    for (const auto& e : result.errors) {
        CHECK(e.find("huge") == std::string::npos);
    }

    ws.Close(id);
    std::filesystem::remove(path);
}

// ── CompareImages ──────────────────────────────────────────────────────────
// Against two of the frozen corpus goldens (Tests/Golden/corpus) -- the same
// PNGs VkOracleParity gates against, giving this a real, non-synthetic
// image to decode.

namespace {
std::filesystem::path CorpusGolden(const char* name) {
    // Tests/Golden/corpus lives two levels up from the onyx_tests binary's
    // usual ctest working directory (CMAKE_SOURCE_DIR/Tests/Golden/corpus);
    // resolved from this TU's own source path instead of a relative guess
    // so it works regardless of ctest's working directory.
    return std::filesystem::path(__FILE__).parent_path() / "Golden" / "corpus" / name;
}
} // namespace

TEST_CASE("TestKit: CompareImages passes an identical corpus golden against itself") {
    auto path = CorpusGolden("blend-stack.png");
    REQUIRE(std::filesystem::exists(path));

    Onyx::TestKit::CompareTolerance tol{8, 0.0, 0.0, 0.0};
    Onyx::TestKit::CompareResult result = Onyx::TestKit::CompareImages(path, path, tol);
    CHECK(result.pass);
    CHECK(result.maxDelta == 0);
    CHECK(result.differingPct == 0.0);
}

TEST_CASE("TestKit: CompareImages fails two differing corpus goldens, naming the knob") {
    // "A golden vs. a deliberately perturbed copy" (task-1 brief) without
    // synthesizing a new PNG (CompareImages only reads -- see
    // RenderCompare.h's design note on why PngWrite stays tool-side): two
    // DIFFERENT rendered scenes at the same frame size (all corpus PNGs are
    // 512x512) stand in for "golden vs. perturbed" here -- a real, large
    // per-pixel divergence that reaches the pixel-compare path rather than
    // the dimension-mismatch short-circuit.
    auto goldenPath = CorpusGolden("blend-stack.png");
    auto otherPath = CorpusGolden("sphere-grid-textured.png");
    REQUIRE(std::filesystem::exists(goldenPath));
    REQUIRE(std::filesystem::exists(otherPath));

    Onyx::TestKit::CompareTolerance tight{0, 0.0, 0.0, 0.0};
    Onyx::TestKit::CompareResult result =
        Onyx::TestKit::CompareImages(goldenPath, otherPath, tight);
    CHECK_FALSE(result.pass);
    CHECK_FALSE(result.message.empty());
    CHECK(result.message != "within tolerance");
    // Two different scenes diverge on every one of the four tiers at a
    // maxChannelDelta==0 tolerance -- the message must name at least the
    // maxChannelDelta tier specifically, not just a generic failure.
    CHECK(result.message.find("maxChannelDelta") != std::string::npos);
}
