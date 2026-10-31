#include <doctest/doctest.h>

#include <Prism/Palette.h>

#include "Support/Harness.h"
#include "Support/PrismFixture.h"

#include <Onyx/Core/Schema/SchemaJson.h>
#include <Onyx/Core/Schema/SchemaNode.h>

#include <cstdint>
#include <memory>
#include <string>

using namespace Onyx;
using Tests::Decoding;
using Tests::Find;
using Tests::HasDiag;
using Tests::Bytes;
using Tests::NewWorkspace;
using Tests::RawPrism;
using Tests::RawSection;
using Tests::SamplePrism;
using Tests::TempFile;

namespace {

uint8_t Byte(const std::vector<std::byte>& pixels, size_t at) {
    return static_cast<uint8_t>(pixels[at]);
}

}

TEST_CASE("the palette decodes as a table and as the Any payload") {
    TempFile file("palette.prm", SamplePrism());
    auto ws = NewWorkspace();
    Modules::Document* doc = ws->Get(ws->Open(file.path));
    REQUIRE(doc);
    const Domain::AssetEntry* entry = Find(*doc, "palette");
    REQUIRE(entry);

    const Modules::Capabilities caps = ws->Decoders().Of(entry->type);
    CHECK(caps.table);
    REQUIRE(caps.any.size() == 1);
    CHECK(caps.any[0] == "prism.palette");

    Decoding table(*ws, *doc, *entry);
    const auto decoded = ws->Decoders().DecodeTable(table.ctx);
    REQUIRE(decoded);
    CHECK(decoded->columns == std::vector<std::string>{"index", "r", "g", "b", "a"});
    REQUIRE(decoded->rows.size() == 4);
    CHECK(decoded->rows[0] == std::vector<std::string>{"0", "255", "0", "0", "255"});
    CHECK(decoded->rows[3] == std::vector<std::string>{"3", "0", "0", "0", "0"});

    Decoding any(*ws, *doc, *entry);
    const std::shared_ptr<void> payload = ws->Decoders().DecodeAny(any.ctx, "prism.palette");
    REQUIRE(payload);
    const auto palette = std::static_pointer_cast<Prism::Palette>(payload);
    REQUIRE(palette->colors.size() == 4);
    CHECK(palette->colors[2] == glm::u8vec4(0, 0, 255, 255));
}

TEST_CASE("the image decodes through the palette and honours prism.transparentIndexZero") {
    TempFile file("image.prm", SamplePrism());
    auto ws = NewWorkspace();
    Modules::Document* doc = ws->Get(ws->Open(file.path));
    REQUIRE(doc);
    const Domain::AssetEntry* entry = Find(*doc, "tiles");
    REQUIRE(entry);

    {
        Decoding image(*ws, *doc, *entry);
        const auto texture = ws->Decoders().DecodeImage(image.ctx);
        REQUIRE(texture);
        CHECK(texture->width == 4);
        CHECK(texture->height == 4);
        CHECK(texture->IsValid());
        // Row 0 reads red, green, blue, transparent black.
        CHECK(Byte(texture->pixels, 0) == 255);
        CHECK(Byte(texture->pixels, 3) == 255);
        CHECK(Byte(texture->pixels, 5) == 255);
        CHECK(Byte(texture->pixels, 8 + 2) == 255);
        CHECK(Byte(texture->pixels, 12 + 3) == 0);
        // The last row repeats the first.
        CHECK(Byte(texture->pixels, 3 * 16) == 255);
    }

    ws->Settings().Set("prism.transparentIndexZero", true);
    Decoding image(*ws, *doc, *entry);
    const auto texture = ws->Decoders().DecodeImage(image.ctx);
    REQUIRE(texture);
    CHECK(Byte(texture->pixels, 0) == 0);
    CHECK(Byte(texture->pixels, 3) == 0);
    CHECK(Byte(texture->pixels, 5) == 255);
}

TEST_CASE("an index past the palette fails the image once") {
    Prism::Writer w;
    w.SetPalette({{1, 2, 3, 255}, {4, 5, 6, 255}});
    w.AddImage("wild", 2, 2, {0, 1, 7, 9});
    TempFile file("wild.prm", w.Build());
    auto ws = NewWorkspace();
    Modules::Document* doc = ws->Get(ws->Open(file.path));
    REQUIRE(doc);
    const Domain::AssetEntry* entry = Find(*doc, "wild");
    REQUIRE(entry);

    Decoding image(*ws, *doc, *entry);
    CHECK_FALSE(ws->Decoders().DecodeImage(image.ctx));
    CHECK(HasDiag(*doc, "prism.image.index", Services::Severity::Error));
    CHECK(doc->diags.Count(Services::Severity::Error) == 1);
}

TEST_CASE("the clip decodes with prism.gain applied") {
    TempFile file("clip.prm", SamplePrism());
    auto ws = NewWorkspace();
    Modules::Document* doc = ws->Get(ws->Open(file.path));
    REQUIRE(doc);
    const Domain::AssetEntry* entry = Find(*doc, "clip");
    REQUIRE(entry);

    float unity = 0.0f;
    {
        Decoding clip(*ws, *doc, *entry);
        const auto audio = ws->Decoders().DecodeAudio(clip.ctx);
        REQUIRE(audio);
        CHECK(audio->sampleRate == 8000);
        CHECK(audio->channels == 1);
        REQUIRE(audio->samples.size() == 800);
        CHECK(audio->samples[0] == doctest::Approx(0.0f));
        unity = audio->samples[100];
        CHECK(unity == doctest::Approx(4000.0f / 32768.0f));
    }

    ws->Settings().Set("prism.gain", 2.0);
    Decoding clip(*ws, *doc, *entry);
    const auto audio = ws->Decoders().DecodeAudio(clip.ctx);
    REQUIRE(audio);
    CHECK(audio->samples[100] == doctest::Approx(unity * 2.0f));
}

TEST_CASE("the manifest is toml text") {
    TempFile file("manifest.prm", SamplePrism());
    auto ws = NewWorkspace();
    Modules::Document* doc = ws->Get(ws->Open(file.path));
    REQUIRE(doc);
    const Domain::AssetEntry* entry = Find(*doc, "manifest");
    REQUIRE(entry);

    Decoding manifest(*ws, *doc, *entry);
    const auto text = ws->Decoders().DecodeText(manifest.ctx);
    REQUIRE(text);
    CHECK(text->language == "toml");
    CHECK(text->text == "title = \"sample\"\n");
}

TEST_CASE("the entries table keeps its columns") {
    TempFile file("entries.prm", SamplePrism());
    auto ws = NewWorkspace();
    Modules::Document* doc = ws->Get(ws->Open(file.path));
    REQUIRE(doc);
    const Domain::AssetEntry* entry = Find(*doc, "entries");
    REQUIRE(entry);

    Decoding entries(*ws, *doc, *entry);
    const auto table = ws->Decoders().DecodeTable(entries.ctx);
    REQUIRE(table);
    CHECK(table->columns == std::vector<std::string>{"id", "name", "note"});
    REQUIRE(table->rows.size() == 1);
    CHECK(table->rows[0] == std::vector<std::string>{"1", "one", "first"});
}

TEST_CASE("the stats schema reads the four fields") {
    TempFile file("stats.prm", SamplePrism());
    auto ws = NewWorkspace();
    Modules::Document* doc = ws->Get(ws->Open(file.path));
    REQUIRE(doc);
    const Domain::AssetEntry* entry = Find(*doc, "stats");
    REQUIRE(entry);

    CHECK(ws->Decoders().Of(entry->type).schema);
    const auto schema = ws->Decoders().SchemaOf(entry->type);
    REQUIRE(schema);
    CHECK(schema->Name() == "PrismStats");

    Decoding stats(*ws, *doc, *entry);
    const auto node = Schema::ReadSchema(*schema, stats.ctx.reader, 0);
    REQUIRE(node.ok());
    const std::string json = Schema::ToJson(node.value());
    CHECK(json.find("\"imageCount\":1") != std::string::npos);
    CHECK(json.find("\"clipCount\":1") != std::string::npos);
    CHECK(json.find("\"gainDefault\":1") != std::string::npos);
    CHECK(json.find("\"flags\":0") != std::string::npos);
}

namespace {

// Opens a file holding one palette and one section named "subject" of the given kind.
struct Subject {
    TempFile file;
    std::unique_ptr<Modules::Workspace> ws = NewWorkspace();
    Modules::Document* doc = nullptr;
    const Domain::AssetEntry* entry = nullptr;

    Subject(const std::string& name, uint8_t kind, const std::vector<std::byte>& payload)
        : file(name, RawPrism(1, {RawSection{0, "palette", Bytes({1, 2, 3, 255})}, RawSection{kind, "subject", payload}})) {
        doc = ws->Get(ws->Open(file.path));
        REQUIRE(doc);
        entry = Find(*doc, "subject");
        REQUIRE(entry);
    }
};

}

TEST_CASE("a palette payload that is not a multiple of 4 fails prism.palette.size on decode") {
    Subject s("palettedecode.prm", 0, Bytes({1, 2, 3, 4, 5, 6}));
    Decoding table(*s.ws, *s.doc, *s.entry);
    CHECK_FALSE(s.ws->Decoders().DecodeTable(table.ctx));
    Decoding any(*s.ws, *s.doc, *s.entry);
    CHECK_FALSE(s.ws->Decoders().DecodeAny(any.ctx, "prism.palette"));
    CHECK(HasDiag(*s.doc, "prism.palette.size", Services::Severity::Error));
}

TEST_CASE("an image shorter than its header fails prism.image.truncated") {
    Subject s("imagetrunc.prm", 1, Bytes({2, 0}));
    Decoding image(*s.ws, *s.doc, *s.entry);
    CHECK_FALSE(s.ws->Decoders().DecodeImage(image.ctx));
    CHECK(HasDiag(*s.doc, "prism.image.truncated", Services::Severity::Error));
}

TEST_CASE("an image whose payload does not match its size fails prism.image.size on decode") {
    Subject s("imagesize.prm", 1, Bytes({2, 0, 2, 0, 0}));
    Decoding image(*s.ws, *s.doc, *s.entry);
    CHECK_FALSE(s.ws->Decoders().DecodeImage(image.ctx));
    CHECK(HasDiag(*s.doc, "prism.image.size", Services::Severity::Error));
}

TEST_CASE("a clip shorter than its header fails prism.clip.truncated") {
    Subject s("cliptrunc.prm", 3, Bytes({0x40, 0x1F, 0, 0, 1}));
    Decoding clip(*s.ws, *s.doc, *s.entry);
    CHECK_FALSE(s.ws->Decoders().DecodeAudio(clip.ctx));
    CHECK(HasDiag(*s.doc, "prism.clip.truncated", Services::Severity::Error));
}

TEST_CASE("a clip whose frame count does not match fails prism.clip.size on decode") {
    Subject s("clipdecode.prm", 3, Bytes({0x40, 0x1F, 0, 0, 1, 0, 5, 0, 0, 0, 1, 0}));
    Decoding clip(*s.ws, *s.doc, *s.entry);
    CHECK_FALSE(s.ws->Decoders().DecodeAudio(clip.ctx));
    CHECK(HasDiag(*s.doc, "prism.clip.size", Services::Severity::Error));
}

TEST_CASE("an entries table shorter than its header fails prism.entries.truncated") {
    Subject s("entriestrunc.prm", 4, Bytes({2, 0}));
    Decoding entries(*s.ws, *s.doc, *s.entry);
    CHECK_FALSE(s.ws->Decoders().DecodeTable(entries.ctx));
    CHECK(HasDiag(*s.doc, "prism.entries.truncated", Services::Severity::Error));
}

TEST_CASE("an entries table declaring more cells than fit fails prism.entries.size") {
    Subject s("entriessize.prm", 4, Bytes({2, 0, 2, 0, 0, 0}));
    Decoding entries(*s.ws, *s.doc, *s.entry);
    CHECK_FALSE(s.ws->Decoders().DecodeTable(entries.ctx));
    CHECK(HasDiag(*s.doc, "prism.entries.size", Services::Severity::Error));
}

TEST_CASE("an entries cell running past the payload fails prism.entries.truncated") {
    Subject s("entriescell.prm", 4, Bytes({1, 0, 1, 0, 9, 0, 'a'}));
    Decoding entries(*s.ws, *s.doc, *s.entry);
    CHECK_FALSE(s.ws->Decoders().DecodeTable(entries.ctx));
    CHECK(HasDiag(*s.doc, "prism.entries.truncated", Services::Severity::Error));
}
