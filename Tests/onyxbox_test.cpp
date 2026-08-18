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
