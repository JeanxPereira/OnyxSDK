#include <doctest/doctest.h>

#include <OnyxBox/Module.h>
#include <OnyxBox/Writer.h>

#include <Onyx/Core/Modules/Workspace.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>
#include "TempDir.h"

using namespace Onyx;
using Modules::DecodeContext;
using Modules::Document;
using Modules::Workspace;

namespace {

struct TempFile {
    Onyx::Test::TempDir scope{"obxcore"};
    std::filesystem::path path;

    TempFile(const std::string& name, const std::vector<std::byte>& bytes) : path(scope / name) {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
};

std::vector<std::byte> Bytes(const std::string& text) {
    std::vector<std::byte> out(text.size());
    std::transform(text.begin(), text.end(), out.begin(), [](char c) { return static_cast<std::byte>(c); });
    return out;
}

std::vector<std::byte> Gradient(size_t count) {
    std::vector<std::byte> out(count);
    for (size_t i = 0; i < count; ++i) out[i] = static_cast<std::byte>(i * 7 & 0xFF);
    return out;
}

std::vector<std::byte> SampleBox() {
    OnyxBox::Writer w;
    w.AddImage("img", 8, 8, Gradient(256));
    w.AddText("txt", "hello box");
    w.AddRange("bad", OnyxBox::Kind::Blob, 0x100000, 4);
    return w.Build();
}

struct Decoding {
    Services::Progress progress;
    DecodeContext ctx;

    Decoding(Workspace& ws, Document& doc, const Domain::AssetEntry& entry)
        : ctx{&doc,
              entry,
              Vfs::Reader(doc.files[entry.source.fileIndex], entry.source.offset, entry.source.size),
              doc.diags,
              progress,
              ws.Settings(),
              doc.moduleState} {}
};

std::unique_ptr<Workspace> NewWorkspace() {
    auto ws = std::make_unique<Workspace>();
    REQUIRE(ws->AddModule(std::make_unique<OnyxBox::Module>()).ok());
    return ws;
}

bool HasDiag(Document& doc, const std::string& code, Services::Severity severity) {
    for (const Services::Diag& d : doc.diags.Snapshot()) {
        if (d.code == code && d.severity == severity) return true;
    }
    return false;
}

const Domain::AssetEntry* Child(const Domain::AssetEntry& parent, const std::string& name) {
    for (const Domain::AssetEntry& c : parent.children) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

}

TEST_CASE("OnyxBox.ProbeOpenTreeDecodeAndSalvage") {
    TempFile file("sample.obx", SampleBox());
    auto ws = NewWorkspace();

    const Modules::ProbeRanking rank = ws->Probe(file.path);
    REQUIRE(rank.winner != nullptr);
    CHECK(rank.rows[0].result.confidence == 95);

    const Modules::DocumentId id = ws->Open(file.path);
    Document* doc = ws->Get(id);
    REQUIRE(doc);
    REQUIRE(doc->roots.size() == 3);
    CHECK(doc->roots[2].flags == Domain::NodeFlags::Failed);
    CHECK(doc->diags.Count(Services::Severity::Error) == 1);

    CHECK(ws->Decoders().Of(doc->roots[0].type).image);
    Decoding image(*ws, *doc, doc->roots[0]);
    const auto texture = ws->Decoders().DecodeImage(image.ctx);
    REQUIRE(texture);
    CHECK(texture->width == 8);
    CHECK(texture->height == 8);
    CHECK(texture->IsValid());
    CHECK(texture->pixels[9] == static_cast<std::byte>(9 * 7));

    Decoding text(*ws, *doc, doc->roots[1]);
    const auto decoded = ws->Decoders().DecodeText(text.ctx);
    REQUIRE(decoded);
    CHECK(decoded->text == "hello box");
}

TEST_CASE("OnyxBox.TypesAreMintedInTheObxNamespace") {
    auto ws = NewWorkspace();
    Types::TypeCatalog& catalog = ws->Catalog();

    CHECK(catalog.Find("obx.blob").Valid());
    CHECK(catalog.Find("obx.image").Valid());
    CHECK(catalog.Find("obx.text").Valid());
    CHECK(catalog.Find("obx.mesh").Valid());
    CHECK(catalog.Find("obx.table").Valid());
    CHECK(catalog.Media(catalog.Find("obx.blob")) == Domain::MediaKind::Raw);
    CHECK(catalog.Media(catalog.Find("obx.image")) == Domain::MediaKind::Image);
    CHECK(catalog.Media(catalog.Find("obx.text")) == Domain::MediaKind::Text);
    CHECK(catalog.Media(catalog.Find("obx.mesh")) == Domain::MediaKind::Mesh);
    CHECK(catalog.Media(catalog.Find("obx.table")) == Domain::MediaKind::Table);
}

TEST_CASE("OnyxBox.ProbeFallsBackToTheExtension") {
    OnyxBox::Module module;
    Modules::ProbeInput input;
    input.path = "thing.OBX";
    CHECK(module.Probe(input).confidence == 20);
    input.path = "thing.bin";
    CHECK(module.Probe(input).confidence == 0);

    const std::vector<std::byte> magic = Bytes("OBP1");
    input.header = magic;
    CHECK(module.Probe(input).confidence == 95);
}

TEST_CASE("OnyxBox.RejectsAFileWithoutTheMagic") {
    TempFile file("garbage.obx", Bytes("not a container at all"));
    auto ws = NewWorkspace();

    const Modules::DocumentId id = ws->Open(file.path, "obx");
    Document* doc = ws->Get(id);
    REQUIRE(doc);
    CHECK(doc->roots.empty());
    CHECK(HasDiag(*doc, "obx.header.bad", Services::Severity::Error));
}

TEST_CASE("OnyxBox.ClampsALyingTocCountInsteadOfTrustingItForAllocation") {
    std::vector<std::byte> bytes = OnyxBox::Writer().Build();
    REQUIRE(bytes.size() == 8);
    for (size_t i = 4; i < 8; ++i) bytes[i] = std::byte{0xFF};
    bytes[4] = std::byte{0xF0};
    TempFile file("lying_count.obx", bytes);
    auto ws = NewWorkspace();

    const Modules::DocumentId id = ws->Open(file.path);
    REQUIRE(id != 0);
    Document* doc = ws->Get(id);
    REQUIRE(doc);
    CHECK(doc->roots.empty());
    CHECK(HasDiag(*doc, "obx.toc.count-clamped", Services::Severity::Warning));
}

TEST_CASE("OnyxBox.RejectsAnImageWhosePayloadSizeDoesNotMatchTheDeclaredDimensions") {
    OnyxBox::Writer w;
    std::vector<std::byte> liar = {std::byte{8}, std::byte{0}, std::byte{8}, std::byte{0}};
    liar.insert(liar.end(), 6, std::byte{0xAA});
    w.AddEntry("liar", OnyxBox::Kind::Image, liar);
    w.AddText("safe", "SAFE_TEXT");
    w.AddBlob("padding", std::vector<std::byte>(512, std::byte{0xEE}));
    TempFile file("lying_image.obx", w.Build());
    auto ws = NewWorkspace();

    const Modules::DocumentId id = ws->Open(file.path);
    Document* doc = ws->Get(id);
    REQUIRE(doc);
    REQUIRE(doc->roots.size() == 3);
    doc->diags.Drain();

    Decoding image(*ws, *doc, doc->roots[0]);
    CHECK_FALSE(ws->Decoders().DecodeImage(image.ctx).has_value());

    bool sawMismatch = false;
    for (const Services::Diag& d : doc->diags.Drain()) {
        if (d.code != "obx.image.size-mismatch") continue;
        sawMismatch = true;
        CHECK(d.severity == Services::Severity::Error);
        CHECK(d.message.find("liar") != std::string::npos);
    }
    CHECK(sawMismatch);

    Decoding text(*ws, *doc, doc->roots[1]);
    const auto decoded = ws->Decoders().DecodeText(text.ctx);
    REQUIRE(decoded);
    CHECK(decoded->text == "SAFE_TEXT");
}

TEST_CASE("OnyxBox.DecodesAMeshEntryIntoACubeScene") {
    OnyxBox::Writer w;
    w.AddMesh("cube", {0.8f, 0.2f, 0.1f, 1.0f}, {5.0f, 6.0f, 7.0f}, {1.0f, 2.0f, 3.0f});
    TempFile file("mesh.obx", w.Build());
    auto ws = NewWorkspace();

    const Modules::DocumentId id = ws->Open(file.path);
    Document* doc = ws->Get(id);
    REQUIRE(doc);
    REQUIRE(doc->roots.size() == 1);
    CHECK(doc->roots[0].flags == Domain::NodeFlags::None);
    CHECK(doc->roots[0].kind == Domain::MediaKind::Mesh);
    CHECK(ws->Decoders().Of(doc->roots[0].type).scene);

    Decoding mesh(*ws, *doc, doc->roots[0]);
    const std::unique_ptr<Domain::Scene> scene = ws->Decoders().DecodeScene(mesh.ctx);
    REQUIRE(scene);
    REQUIRE(scene->parts.size() == 1);
    const Domain::MeshPart& part = scene->parts[0];
    CHECK(part.positions.size() == 24);
    CHECK(part.normals.size() == 24);
    CHECK(part.indices.size() == 36);
    CHECK(part.materialIndex == 0);
    REQUIRE(scene->materials.size() == 1);
    CHECK(scene->materials[0].baseColor.r == doctest::Approx(0.8f));
    CHECK(scene->materials[0].baseColor.g == doctest::Approx(0.2f));
    CHECK(scene->materials[0].baseColor.b == doctest::Approx(0.1f));
    CHECK(scene->materials[0].baseColor.a == doctest::Approx(1.0f));

    for (const glm::vec3& p : part.positions) {
        CHECK(p.x >= doctest::Approx(4.0f));
        CHECK(p.x <= doctest::Approx(6.0f));
        CHECK(p.y >= doctest::Approx(4.0f));
        CHECK(p.y <= doctest::Approx(8.0f));
        CHECK(p.z >= doctest::Approx(4.0f));
        CHECK(p.z <= doctest::Approx(10.0f));
    }
    for (uint32_t index : part.indices) CHECK(index < part.positions.size());
    CHECK(scene->bounds.min.x == doctest::Approx(4.0f));
    CHECK(scene->bounds.max.z == doctest::Approx(10.0f));
}

TEST_CASE("OnyxBox.FlagsAMeshEntryFailedWhenThePayloadIsNot48Bytes") {
    OnyxBox::Writer w;
    w.AddEntry("bad", OnyxBox::Kind::Mesh, std::vector<std::byte>(40, std::byte{0xAB}));
    TempFile file("mesh_bad_size.obx", w.Build());
    auto ws = NewWorkspace();

    const Modules::DocumentId id = ws->Open(file.path);
    Document* doc = ws->Get(id);
    REQUIRE(doc);
    REQUIRE(doc->roots.size() == 1);
    CHECK(doc->roots[0].flags == Domain::NodeFlags::Failed);
    CHECK(HasDiag(*doc, "obx.mesh.size", Services::Severity::Error));

    Decoding mesh(*ws, *doc, doc->roots[0]);
    CHECK(ws->Decoders().DecodeScene(mesh.ctx) == nullptr);
}

TEST_CASE("OnyxBox.TableEntryDecodes") {
    OnyxBox::Writer w;
    w.AddTable("tbl", {{"alpha", "beta"}, {"", "gamma"}, {"caf\xC3\xA9", "4"}});
    TempFile file("table.obx", w.Build());
    auto ws = NewWorkspace();

    const Modules::DocumentId id = ws->Open(file.path);
    Document* doc = ws->Get(id);
    REQUIRE(doc);
    REQUIRE(doc->roots.size() == 1);
    CHECK(doc->roots[0].flags == Domain::NodeFlags::None);
    CHECK(doc->roots[0].kind == Domain::MediaKind::Table);
    CHECK(ws->Decoders().Of(doc->roots[0].type).table);

    Decoding table(*ws, *doc, doc->roots[0]);
    const auto decoded = ws->Decoders().DecodeTable(table.ctx);
    REQUIRE(decoded);
    CHECK(decoded->columns == std::vector<std::string>{"col0", "col1"});
    REQUIRE(decoded->rows.size() == 3);
    CHECK(decoded->rows[0] == std::vector<std::string>{"alpha", "beta"});
    CHECK(decoded->rows[1] == std::vector<std::string>{"", "gamma"});
    CHECK(decoded->rows[2] == std::vector<std::string>{"caf\xC3\xA9", "4"});
}

TEST_CASE("OnyxBox.TableWithLyingDimensionsFailsToDecode") {
    OnyxBox::Writer w;
    w.AddEntry("tbl", OnyxBox::Kind::Table,
               {std::byte{0xE8}, std::byte{0x03}, std::byte{0xE8}, std::byte{0x03}, std::byte{1}, std::byte{0}});
    w.AddEntry("cut", OnyxBox::Kind::Table,
               {std::byte{1}, std::byte{0}, std::byte{1}, std::byte{0}, std::byte{9}, std::byte{0}, std::byte{'x'}});
    TempFile file("table_lying.obx", w.Build());
    auto ws = NewWorkspace();

    const Modules::DocumentId id = ws->Open(file.path);
    Document* doc = ws->Get(id);
    REQUIRE(doc);
    REQUIRE(doc->roots.size() == 2);

    Decoding lying(*ws, *doc, doc->roots[0]);
    CHECK_FALSE(ws->Decoders().DecodeTable(lying.ctx).has_value());
    CHECK(HasDiag(*doc, "obx.table.size", Services::Severity::Error));

    Decoding cut(*ws, *doc, doc->roots[1]);
    CHECK_FALSE(ws->Decoders().DecodeTable(cut.ctx).has_value());
    CHECK(HasDiag(*doc, "obx.table.truncated", Services::Severity::Error));
}

TEST_CASE("OnyxBox.SettingsDeclareStrictTocOffByDefault") {
    auto ws = NewWorkspace();
    const auto& entries = ws->ModuleSettings("obx").Entries();

    const auto it = std::find_if(entries.begin(), entries.end(),
                                 [](const Services::SettingsSchema::Entry& e) { return e.key == "obx.strictToc"; });
    REQUIRE(it != entries.end());
    CHECK(it->kind == Services::SettingsSchema::Kind::Bool);
    CHECK(std::get<bool>(it->def) == false);
}

TEST_CASE("OnyxBox.StrictTocFailsAParseThatHadToSalvage") {
    std::vector<std::byte> bytes = OnyxBox::Writer().Build();
    bytes[4] = std::byte{2};
    TempFile file("strict.obx", bytes);

    {
        auto ws = NewWorkspace();
        bool ready = false;
        bool ok = true;
        const auto sub = ws->Events().On<Modules::TreeReady>([&](const Modules::TreeReady& e) {
            ready = true;
            ok = e.ok;
        });
        ws->Open(file.path);
        ws->Pump();
        CHECK(ready);
        CHECK(ok);
    }
    {
        auto ws = NewWorkspace();
        ws->Settings().Set("obx.strictToc", true);
        bool ok = true;
        const auto sub = ws->Events().On<Modules::TreeReady>([&](const Modules::TreeReady& e) { ok = e.ok; });
        ws->Open(file.path);
        ws->Pump();
        CHECK_FALSE(ok);
    }
}

TEST_CASE("Mounts.ObxPakMountsEveryInnerContainerAsASubtree") {
    OnyxBox::Writer a;
    a.AddText("shared.txt", "FROM-BOX-A");
    OnyxBox::Writer b;
    b.AddText("shared.txt", "FROM-BOX-B");
    b.AddRange("corrupt.bin", OnyxBox::Kind::Blob, 0x100000, 4);
    TempFile file("pak.obxpak", OnyxBox::Writer::BuildPak({{"boxA.obx", a.Build()}, {"boxB.obx", b.Build()}}));
    auto ws = NewWorkspace();

    const Modules::ProbeRanking rank = ws->Probe(file.path);
    REQUIRE(rank.winner != nullptr);
    CHECK(rank.winner->Info().id == "obx");
    CHECK(rank.rows[0].result.confidence == 95);

    const Modules::DocumentId id = ws->Open(file.path);
    Document* doc = ws->Get(id);
    REQUIRE(doc);
    CHECK(doc->mounted != nullptr);
    REQUIRE(doc->roots.size() == 2);
    CHECK(doc->files.size() == 3);
    CHECK(doc->roots[0].name == "boxA.obx");
    CHECK(doc->roots[0].kind == Domain::MediaKind::Container);
    CHECK(doc->roots[1].name == "boxB.obx");

    const Domain::AssetEntry* sharedB = Child(doc->roots[1], "shared.txt");
    REQUIRE(sharedB);
    CHECK(sharedB->source.fileIndex == 2);
    const Domain::AssetEntry* corrupt = Child(doc->roots[1], "corrupt.bin");
    REQUIRE(corrupt);
    CHECK(corrupt->flags == Domain::NodeFlags::Failed);
    CHECK(HasDiag(*doc, "obx.entry.range", Services::Severity::Error));
}

TEST_CASE("Mounts.AnEmptyMountedPakReportsExactlyOneWarningAndNoRoots") {
    TempFile file("empty.obxpak", OnyxBox::Writer::BuildPak({}));
    auto ws = NewWorkspace();

    const Modules::DocumentId id = ws->Open(file.path);
    Document* doc = ws->Get(id);
    REQUIRE(doc);
    CHECK(doc->roots.empty());

    int warnings = 0;
    bool sawEmpty = false;
    for (const Services::Diag& d : doc->diags.Snapshot()) {
        if (d.severity == Services::Severity::Warning) ++warnings;
        if (d.code == "obx.pak.empty") sawEmpty = true;
    }
    CHECK(warnings == 1);
    CHECK(sawEmpty);
}

TEST_CASE("Mounts.DecodingBoxBsSharedTxtByEntryReturnsBoxBsBytes") {
    OnyxBox::Writer a;
    a.AddText("shared.txt", "FROM-BOX-A");
    OnyxBox::Writer b;
    b.AddText("shared.txt", "FROM-BOX-B");
    TempFile file("disambiguate.obxpak", OnyxBox::Writer::BuildPak({{"boxA.obx", a.Build()}, {"boxB.obx", b.Build()}}));
    auto ws = NewWorkspace();

    const Modules::DocumentId id = ws->Open(file.path);
    Document* doc = ws->Get(id);
    REQUIRE(doc);
    REQUIRE(doc->roots.size() == 2);
    const Domain::AssetEntry* sharedA = Child(doc->roots[0], "shared.txt");
    const Domain::AssetEntry* sharedB = Child(doc->roots[1], "shared.txt");
    REQUIRE(sharedA);
    REQUIRE(sharedB);

    Decoding fromB(*ws, *doc, *sharedB);
    const auto textB = ws->Decoders().DecodeText(fromB.ctx);
    REQUIRE(textB);
    CHECK(textB->text == "FROM-BOX-B");

    Decoding fromA(*ws, *doc, *sharedA);
    const auto textA = ws->Decoders().DecodeText(fromA.ctx);
    REQUIRE(textA);
    CHECK(textA->text == "FROM-BOX-A");
}

TEST_CASE("Mounts.TheMountSpecClaimsObxpakAndServesInnerContainers") {
    OnyxBox::Writer a;
    a.AddText("t", "x");
    const std::vector<std::byte> inner = a.Build();
    TempFile file("spec.obxpak", OnyxBox::Writer::BuildPak({{"a.obx", inner}}));

    const std::vector<Modules::MountSpec> specs = OnyxBox::Module().Mounts();
    REQUIRE(specs.size() == 1);
    CHECK(specs[0].label == "OnyxBox pak");
    CHECK(specs[0].extensions == std::vector<std::string>{"obxpak"});

    const std::shared_ptr<Vfs::IVirtualFileSystem> vfs = specs[0].mount(file.path);
    REQUIRE(vfs);
    const std::vector<Vfs::DirEntry> listing = vfs->List("");
    REQUIRE(listing.size() == 1);
    CHECK(listing[0].name == "a.obx");
    CHECK_FALSE(listing[0].isDirectory);
    CHECK(listing[0].size == inner.size());
    CHECK(vfs->Exists("a.obx"));
    CHECK_FALSE(vfs->Exists("b.obx"));
    CHECK(vfs->Open("b.obx") == nullptr);

    const std::shared_ptr<Vfs::IFile> opened = vfs->Open("a.obx");
    REQUIRE(opened);
    CHECK(opened->Size() == inner.size());
    std::vector<std::byte> magic(4);
    CHECK(opened->ReadAt(0, magic) == 4);
    CHECK(magic == Bytes("OBX1"));
}

TEST_CASE("Mounts.ATruncatedPakKeepsTheContainersParsedBeforeTheCut") {
    OnyxBox::Writer a;
    a.AddText("t", "x");
    OnyxBox::Writer b;
    b.AddText("t", "y");
    std::vector<std::byte> pak = OnyxBox::Writer::BuildPak({{"a.obx", a.Build()}, {"b.obx", b.Build()}});
    pak.resize(pak.size() - 3);
    TempFile file("cut.obxpak", pak);

    const auto vfs = OnyxBox::Module().Mounts()[0].mount(file.path);
    REQUIRE(vfs);
    const std::vector<Vfs::DirEntry> listing = vfs->List("");
    REQUIRE(listing.size() == 1);
    CHECK(listing[0].name == "a.obx");
}

TEST_CASE("Mounts.ARefusedPakMountFallsThroughToAFlatParse") {
    TempFile file("refused.obxpak", Bytes("OBP"));
    CHECK(OnyxBox::Module().Mounts()[0].mount(file.path) == nullptr);
    CHECK(OnyxBox::Module().Mounts()[0].mount(file.path.string() + ".missing") == nullptr);

    auto ws = NewWorkspace();
    const Modules::DocumentId id = ws->Open(file.path, "obx");
    Document* doc = ws->Get(id);
    REQUIRE(doc);
    CHECK(doc->mounted == nullptr);
    CHECK(doc->roots.empty());
    CHECK(HasDiag(*doc, "obx.mount-refused", Services::Severity::Warning));
    CHECK(HasDiag(*doc, "obx.header.bad", Services::Severity::Error));
}
