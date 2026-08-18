#include <doctest/doctest.h>

#include <OnyxBoxModule.h>

#include <Onyx/Modules/Workspace.h>
#include <Onyx/Types/TypeCatalog.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
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

// Writes a well-formed .obx to a temp file: an 8x8 image ("img", kind=1)
// with a deterministic gradient pixel[i] = uint8_t(i * 7) over the 256 raw
// RGBA bytes, a text entry ("txt", kind=2) holding "hello box", and one
// corrupt entry ("bad", kind=0) whose payloadOffset lands beyond EOF.
std::filesystem::path WriteSampleBox() {
    std::vector<uint8_t> imagePayload;
    PutU16LE(imagePayload, 8);  // width
    PutU16LE(imagePayload, 8);  // height
    for (int i = 0; i < 256; ++i) {
        imagePayload.push_back(uint8_t(i * 7));
    }

    const std::string textPayload = "hello box";

    // magic(4) + count(4) + 3 * (nameLen(2) + name(3) + kind(1) + offset(4) + size(4))
    const uint32_t headerSize = 4 + 4 + 3 * (2 + 3 + 1 + 4 + 4);
    const uint32_t imageOffset = headerSize;
    const uint32_t imageSize = uint32_t(imagePayload.size());
    const uint32_t textOffset = imageOffset + imageSize;
    const uint32_t textSize = uint32_t(textPayload.size());
    const uint32_t fileEnd = textOffset + textSize;
    const uint32_t corruptOffset = fileEnd + 1000;  // deliberately beyond EOF

    std::vector<uint8_t> buf;
    buf.insert(buf.end(), {uint8_t('O'), uint8_t('B'), uint8_t('X'), uint8_t('1')});
    PutU32LE(buf, 3);

    PutTocHeader(buf, "img", 1, imageOffset, imageSize);
    PutTocHeader(buf, "txt", 2, textOffset, textSize);
    PutTocHeader(buf, "bad", 0, corruptOffset, 4);

    buf.insert(buf.end(), imagePayload.begin(), imagePayload.end());
    buf.insert(buf.end(), textPayload.begin(), textPayload.end());

    auto path = std::filesystem::temp_directory_path() / "onyx_onyxbox_sample.obx";
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
    f.close();
    return path;
}

} // namespace

TEST_CASE("onyxbox end to end: probe, open, tree, decode, salvage") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(std::make_unique<OnyxBox::OnyxBoxModule>());
    auto obx = WriteSampleBox();  // helper above
    auto rank = ws.Probe(obx);
    REQUIRE(rank.winner != nullptr);
    CHECK(rank.rows[0].result.confidence == 95);

    auto id = ws.Open(obx);
    auto* doc = ws.Get(id);
    REQUIRE(doc); REQUIRE(doc->roots.size() == 3);
    CHECK(doc->roots[2].flags == Onyx::Domain::NodeFlags::Failed);
    CHECK(doc->diags.Count(Onyx::Services::Severity::Error) == 1);

    // decode image
    auto& reg = ws.Decoders();
    REQUIRE(reg.HasImage(doc->roots[0].typeId));
    Onyx::Services::Progress prog;
    Onyx::Modules::DecodeContext ctx{*doc, doc->roots[0], doc->diags, prog};
    auto img = reg.DecodeImage(ctx);
    REQUIRE(img); CHECK(img->width == 8); CHECK(img->height == 8);
    CHECK(img->pixels[9] == uint8_t(9 * 7));

    // decode text
    Onyx::Modules::DecodeContext ctx2{*doc, doc->roots[1], doc->diags, prog};
    auto txt = reg.DecodeText(ctx2);
    REQUIRE(txt); CHECK(txt->text == "hello box");
    ws.Close(id);  // release the Document's OsFile handle before removing
                   // the temp file -- Windows refuses to delete an open file
                   // (see the identical pattern/comment in workspace_test.cpp).
    std::filesystem::remove(obx);
}

TEST_CASE("onyxbox types are minted in the obx namespace") {
    auto& catalog = Onyx::Types::TypeCatalog::Get();
    Onyx::Modules::Workspace ws(catalog);
    ws.AddModule(std::make_unique<OnyxBox::OnyxBoxModule>());
    CHECK(catalog.Find("obx.image").valid());
}

TEST_CASE("onyxbox clamps a lying TOC count instead of trusting it for allocation") {
    // Header claims ~4 billion entries; the file holds none. A raw
    // toc.reserve(count) would attempt a wild allocation -- the count must
    // be clamped to what the file could physically hold before reserving.
    std::vector<uint8_t> buf;
    buf.insert(buf.end(), {uint8_t('O'), uint8_t('B'), uint8_t('X'), uint8_t('1')});
    PutU32LE(buf, 0xFFFFFFF0u);

    auto path = std::filesystem::temp_directory_path() / "onyx_onyxbox_lying_count.obx";
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
    }

    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(std::make_unique<OnyxBox::OnyxBoxModule>());

    auto id = ws.Open(path);
    REQUIRE(id != 0);
    auto* doc = ws.Get(id);
    REQUIRE(doc);
    CHECK(doc->roots.empty());  // no room in the file for even one entry

    auto diags = doc->diags.Drain();
    bool sawClampDiag = false;
    for (auto& d : diags) {
        if (d.code == "obx.toc.count-clamped") {
            sawClampDiag = true;
            CHECK(d.severity == Onyx::Services::Severity::Warning);
        }
    }
    CHECK(sawClampDiag);

    ws.Close(id);
    std::filesystem::remove(path);
}

TEST_CASE("onyxbox rejects an image whose payloadSize doesn't match the declared WxH") {
    // "liar": kind=1, payloadSize says 10 bytes, but the embedded header
    // claims 8x8 (needs 4 + 256 = 260 bytes). A "safe" text entry follows
    // immediately after, plus trailing padding, so the *file* is large
    // enough that a fileSize-only bounds check (the old bug) would happily
    // let the pixel read run past the declared 10-byte payload and return
    // the neighbor's (and padding's) bytes as pixels.
    const std::string safeText = "SAFE_TEXT";

    // magic(4) + count(4) + 2 * (nameLen(2) + name(4) + kind(1) + offset(4) + size(4))
    const uint32_t headerSize = 4 + 4 + 2 * (2 + 4 + 1 + 4 + 4);
    const uint32_t liarOffset = headerSize;
    const uint32_t liarSize = 10;
    const uint32_t safeOffset = liarOffset + liarSize;
    const uint32_t safeSize = uint32_t(safeText.size());
    const uint32_t padding = 512;  // pushes fileSize well past liarOffset + 260

    std::vector<uint8_t> buf;
    buf.insert(buf.end(), {uint8_t('O'), uint8_t('B'), uint8_t('X'), uint8_t('1')});
    PutU32LE(buf, 2);
    PutTocHeader(buf, "liar", 1, liarOffset, liarSize);
    PutTocHeader(buf, "safe", 2, safeOffset, safeSize);

    PutU16LE(buf, 8);  // width  -- claims 8x8 ...
    PutU16LE(buf, 8);  // height -- ... which needs 260 bytes, not 10
    for (int i = 0; i < 6; ++i) buf.push_back(uint8_t(0xAA));  // fills out liarSize

    buf.insert(buf.end(), safeText.begin(), safeText.end());
    for (uint32_t i = 0; i < padding; ++i) buf.push_back(uint8_t(0xEE));

    auto path = std::filesystem::temp_directory_path() / "onyx_onyxbox_lying_image.obx";
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
    }

    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(std::make_unique<OnyxBox::OnyxBoxModule>());

    auto id = ws.Open(path);
    REQUIRE(id != 0);
    auto* doc = ws.Get(id);
    REQUIRE(doc);
    REQUIRE(doc->roots.size() == 2);
    doc->diags.Drain();  // discard any parse-time diags before asserting on decode diags

    auto& reg = ws.Decoders();
    Onyx::Services::Progress prog;
    Onyx::Modules::DecodeContext ctx{*doc, doc->roots[0], doc->diags, prog};
    auto img = reg.DecodeImage(ctx);
    CHECK(img == nullptr);

    auto diags = doc->diags.Drain();
    bool sawMismatch = false;
    for (auto& d : diags) {
        if (d.code == "obx.image.size-mismatch") {
            sawMismatch = true;
            CHECK(d.severity == Onyx::Services::Severity::Error);
            CHECK(d.message.find("liar") != std::string::npos);
        }
    }
    CHECK(sawMismatch);

    // The neighboring entry's bytes are untouched by the failed image
    // decode -- decoding it still yields its own real text.
    Onyx::Modules::DecodeContext ctx2{*doc, doc->roots[1], doc->diags, prog};
    auto txt = reg.DecodeText(ctx2);
    REQUIRE(txt);
    CHECK(txt->text == safeText);

    ws.Close(id);
    std::filesystem::remove(path);
}
