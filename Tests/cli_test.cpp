#include <doctest/doctest.h>

#include <Onyx/Cli/Commands.h>
#include <Onyx/Domain/ByteRange.h>
#include <Onyx/Domain/Entry.h>
#include <Onyx/Exchange/GltfExport.h>
#include <Onyx/Modules/Workspace.h>
#include <Onyx/Types/TypeCatalog.h>

#include <OnyxBoxModule.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// spec §5.4: AssetEntry::source is a 24-byte value type (uint32_t fileIndex
// + uint64_t offset + uint64_t size) -- widening it must never silently
// grow the struct beyond that (padding creep, an accidental extra field).
static_assert(sizeof(Onyx::Domain::ByteRange) == 24,
              "ByteRange must stay a tight 24-byte value type");

// Local onyxbox fixtures (adapted from Tests/onyxbox_test.cpp's
// WriteSampleBox pattern -- not included here so this file's TEST_CASE
// set stays independent) writing to distinct temp file names so it never
// races the onyxbox_test.cpp fixtures of the same shape.
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

void PutF32LE(std::vector<uint8_t>& buf, float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    PutU32LE(buf, bits);
}

void PutTocHeader(std::vector<uint8_t>& buf, const std::string& name, uint8_t kind,
                   uint32_t payloadOffset, uint32_t payloadSize) {
    PutU16LE(buf, uint16_t(name.size()));
    buf.insert(buf.end(), name.begin(), name.end());
    buf.push_back(kind);
    PutU32LE(buf, payloadOffset);
    PutU32LE(buf, payloadSize);
}

// An 8x8 image ("img", kind=1), a text entry ("txt", kind=2) holding
// "hello box", and one corrupt entry ("bad", kind=0) whose payloadOffset
// lands beyond EOF (so ParseContainer flags it Failed and reports an
// Error diag -- exercising both the --json "failed" flag and CmdDecode's
// --strict path).
std::filesystem::path WriteSampleBox() {
    std::vector<uint8_t> imagePayload;
    PutU16LE(imagePayload, 8);  // width
    PutU16LE(imagePayload, 8);  // height
    for (int i = 0; i < 256; ++i) {
        imagePayload.push_back(uint8_t(i * 7));
    }

    const std::string textPayload = "hello box";

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

    auto path = std::filesystem::temp_directory_path() / "onyx_cli_sample.obx";
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
    f.close();
    return path;
}

// Five entries, all with valid in-bounds payload ranges (so ParseContainer
// never flags any of them Failed), but four carry hostile names:
// "../evil.txt" (parent-directory escape), "a/b.txt" (nested path), "NUL"
// and "con.txt" (Windows reserved device names, bare and with an
// extension -- both still resolve to the device, not a file). CmdExtract
// must refuse to write any of the four -- only "good.txt" is a safe bare
// filename.
std::filesystem::path WriteUnsafeNamesBox() {
    const std::vector<std::pair<std::string, std::string>> entries = {
        {"good.txt", "safe data"},
        {"../evil.txt", "evil data"},
        {"a/b.txt", "nested data"},
        {"NUL", "device data"},
        {"con.txt", "device ext data"},
    };

    // magic(4) + count(4) + N * (nameLen(2) + name + kind(1) + offset(4) + size(4))
    uint32_t headerSize = 4 + 4;
    for (const auto& [name, text] : entries) {
        headerSize += uint32_t(2 + name.size() + 1 + 4 + 4);
    }

    std::vector<uint8_t> buf;
    buf.insert(buf.end(), {uint8_t('O'), uint8_t('B'), uint8_t('X'), uint8_t('1')});
    PutU32LE(buf, uint32_t(entries.size()));

    uint32_t offset = headerSize;
    for (const auto& [name, text] : entries) {
        PutTocHeader(buf, name, 2, offset, uint32_t(text.size()));
        offset += uint32_t(text.size());
    }
    for (const auto& [name, text] : entries) {
        buf.insert(buf.end(), text.begin(), text.end());
    }

    auto path = std::filesystem::temp_directory_path() / "onyx_cli_unsafe_names.obx";
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
    f.close();
    return path;
}

// A file with neither the OBX1 magic nor the .obx extension -- no module
// registered in these tests has any evidence to claim it.
std::filesystem::path WriteGarbageFile() {
    auto path = std::filesystem::temp_directory_path() / "onyx_cli_garbage.bin";
    std::ofstream f(path, std::ios::binary);
    const char junk[] = "not a container format";
    f.write(junk, sizeof(junk));
    f.close();
    return path;
}

// One entry ("liar", kind=1/image) whose payload fits within the file
// (so ParseContainer never flags it Failed) but whose embedded
// width/height header claims more pixels than its declared payloadSize
// can hold -- adapted from Tests/onyxbox_test.cpp's "onyxbox rejects an
// image whose payloadSize doesn't match the declared WxH" fixture, using
// a distinct temp filename so it never races that file. DecodeImage
// rejects this with an Error diag ("obx.image.size-mismatch") and
// returns nullptr even though reg.HasImage() is true for its type --
// exactly the "capability exists, decoder returns null" salvage-failure
// shape CmdDecode must report as kOk (diags explain), not kUsage.
std::filesystem::path WriteLyingImageBox() {
    const std::string safeText = "SAFE_TEXT";

    // magic(4) + count(4) + 2 * (nameLen(2) + name(4) + kind(1) + offset(4) + size(4))
    const uint32_t headerSize = 4 + 4 + 2 * (2 + 4 + 1 + 4 + 4);
    const uint32_t liarOffset = headerSize;
    const uint32_t liarSize = 10;  // claims 8x8 (needs 260) but only gets 10
    const uint32_t safeOffset = liarOffset + liarSize;
    const uint32_t safeSize = uint32_t(safeText.size());
    const uint32_t padding = 512;

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

    auto path = std::filesystem::temp_directory_path() / "onyx_cli_lying_image.obx";
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
    f.close();
    return path;
}

// One entry ("huge") whose TOC-declared payloadSize is 0xFFFFFFFF (uint32
// max) -- spec §5.4's widening case. The file itself is just the header
// (no payload bytes follow), so the declared range is trivially beyond
// EOF and ParseContainer must flag the entry Failed via the existing
// bounds check -- but the declared size must still land in
// AssetEntry::source.size un-truncated, proving the uint32 TOC value
// widens cleanly into the 64-bit in-memory field the whole way through
// (TOC parse -> AssetEntry assignment), even though the on-disk TOC
// column itself stays uint32 (this fixture never changes that).
std::filesystem::path WriteHugeSizeBox() {
    const std::string name = "huge";
    const uint32_t headerSize = 4 + 4 + uint32_t(2 + name.size() + 1 + 4 + 4);
    const uint32_t payloadOffset = headerSize;
    const uint32_t payloadSize = 0xFFFFFFFF;

    std::vector<uint8_t> buf;
    buf.insert(buf.end(), {uint8_t('O'), uint8_t('B'), uint8_t('X'), uint8_t('1')});
    PutU32LE(buf, 1);
    PutTocHeader(buf, name, 0, payloadOffset, payloadSize);
    // Deliberately no payload bytes -- the file ends right after the TOC,
    // so payloadOffset + payloadSize (huge) is always beyond EOF.

    auto path = std::filesystem::temp_directory_path() / "onyx_cli_huge_size.obx";
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
    f.close();
    return path;
}

// One kind=3 (mesh) entry named "cube": a valid 48-byte (12-float) payload
// -- base color, position, half-extents, 2 padding floats -- decoding to a
// single-part cube (Examples/OnyxBox/OnyxBoxModule.h's top comment).
std::filesystem::path WriteMeshBox() {
    std::vector<uint8_t> payload;
    PutF32LE(payload, 0.8f); PutF32LE(payload, 0.2f); PutF32LE(payload, 0.1f); PutF32LE(payload, 1.0f);
    PutF32LE(payload, 0.0f); PutF32LE(payload, 0.0f); PutF32LE(payload, 0.0f);
    PutF32LE(payload, 1.0f); PutF32LE(payload, 2.0f); PutF32LE(payload, 3.0f);
    PutF32LE(payload, 0.0f); PutF32LE(payload, 0.0f);

    const uint32_t headerSize = 4 + 4 + uint32_t(2 + 4 + 1 + 4 + 4); // 1 entry, name "mesh"
    const uint32_t meshOffset = headerSize;
    const uint32_t meshSize = uint32_t(payload.size());

    std::vector<uint8_t> buf;
    buf.insert(buf.end(), {uint8_t('O'), uint8_t('B'), uint8_t('X'), uint8_t('1')});
    PutU32LE(buf, 1);
    PutTocHeader(buf, "mesh", 3, meshOffset, meshSize);
    buf.insert(buf.end(), payload.begin(), payload.end());

    auto path = std::filesystem::temp_directory_path() / "onyx_cli_mesh.obx";
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
    f.close();
    return path;
}

std::unique_ptr<OnyxBox::OnyxBoxModule> MakeModule() {
    return std::make_unique<OnyxBox::OnyxBoxModule>();
}

// Never claims any file (Probe always scores 0). Registered alongside
// OnyxBoxModule so a "no module accepts" scenario stays genuine: with
// exactly one module registered, Workspace skips probing (and its
// confidence floor) entirely per spec §5.1, so a single-module Workspace
// can no longer be used to exercise the "nobody wins" path.
struct NeverMatchModule : Onyx::Modules::IGameModule {
    Onyx::Modules::ModuleInfo Info() const override {
        return Onyx::Modules::ModuleInfo{"nevermatch", "NeverMatch", {}, {}};
    }
    Onyx::Modules::ProbeResult Probe(const Onyx::Modules::ProbeInput&) const override {
        return Onyx::Modules::ProbeResult{0, "never matches"};
    }
    void RegisterTypes(Onyx::Types::TypeRegistrar&) override {}
    void RegisterDecoders(Onyx::Modules::DecoderRegistry&) override {}
    Onyx::Modules::ParseResult ParseContainer(Onyx::Modules::ContainerContext&) override {
        return Onyx::Modules::ParseResult{false};
    }
};

// Always outranks the real OnyxBox module (99 > obx's 95 for a genuine
// OBX1 file), but its ParseContainer leaves the tree empty (ok=true, zero
// roots) -- proof that Run's "--game <hint>" forces the real module
// through Workspace::Open's hint-wins-outright path, bypassing ranking
// entirely. Without the hint, this decoy would win the probe and the
// caller would get its empty parse instead of the real tree.
struct DecoyModule : Onyx::Modules::IGameModule {
    Onyx::Modules::ModuleInfo Info() const override {
        return Onyx::Modules::ModuleInfo{"decoy", "Decoy", {"decoy"}, {}};
    }
    Onyx::Modules::ProbeResult Probe(const Onyx::Modules::ProbeInput&) const override {
        return Onyx::Modules::ProbeResult{99, "always outranks obx"};
    }
    void RegisterTypes(Onyx::Types::TypeRegistrar&) override {}
    void RegisterDecoders(Onyx::Modules::DecoderRegistry&) override {}
    Onyx::Modules::ParseResult ParseContainer(Onyx::Modules::ContainerContext&) override {
        return Onyx::Modules::ParseResult{true};   // ok, but roots stays empty
    }
};

} // namespace

TEST_CASE("cli probe prints a table with rows and a winner line") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());
    auto path = WriteSampleBox();

    std::ostringstream out;
    int rc = Onyx::Cli::CmdProbe(ws, path, out);
    CHECK(rc == Onyx::Cli::kOk);

    const std::string text = out.str();
    CHECK(text.find("obx") != std::string::npos);
    CHECK(text.find("95") != std::string::npos);
    CHECK(text.find("winner: obx") != std::string::npos);

    std::filesystem::remove(path);
}

TEST_CASE("cli list json contains type key and failed flag for corrupt entry") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());
    auto path = WriteSampleBox();

    std::ostringstream out;
    int rc = Onyx::Cli::CmdList(ws, path, /*json=*/true, out);
    CHECK(rc == Onyx::Cli::kOk);

    const std::string text = out.str();
    CHECK(text.find("\"type\":\"obx.image\"") != std::string::npos);
    CHECK(text.find("\"failed\":true") != std::string::npos);
    CHECK(text.find("\"path\":") != std::string::npos);
    CHECK(text.find("\"module\":\"obx\"") != std::string::npos);

    std::filesystem::remove(path);
}

TEST_CASE("cli extract writes payloadSize bytes per good entry and skips failed entries") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());
    auto path = WriteSampleBox();
    auto outDir = std::filesystem::temp_directory_path() / "onyx_cli_extract_out";
    std::filesystem::remove_all(outDir);

    std::ostringstream out;
    int rc = Onyx::Cli::CmdExtract(ws, path, outDir, out);
    CHECK(rc == Onyx::Cli::kOk);

    REQUIRE(std::filesystem::exists(outDir / "img"));
    REQUIRE(std::filesystem::exists(outDir / "txt"));
    CHECK_FALSE(std::filesystem::exists(outDir / "bad"));

    // image payload = 2(width) + 2(height) + 8*8*4(pixels) = 260 bytes
    CHECK(std::filesystem::file_size(outDir / "img") == 260);
    CHECK(std::filesystem::file_size(outDir / "txt") == std::string("hello box").size());

    std::filesystem::remove_all(outDir);
    std::filesystem::remove(path);
}

TEST_CASE("cli extract skips unsafe entry names and writes nothing outside outDir") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());
    auto path = WriteUnsafeNamesBox();
    auto outDir = std::filesystem::temp_directory_path() / "onyx_cli_unsafe_extract_out";
    std::filesystem::remove_all(outDir);

    std::ostringstream out;
    int rc = Onyx::Cli::CmdExtract(ws, path, outDir, out);
    CHECK(rc == Onyx::Cli::kOk);

    const std::string text = out.str();
    CHECK(text.find("skipped '../evil.txt': unsafe name") != std::string::npos);
    CHECK(text.find("skipped 'a/b.txt': unsafe name") != std::string::npos);
    CHECK(text.find("skipped 'NUL': unsafe name") != std::string::npos);
    CHECK(text.find("skipped 'con.txt': unsafe name") != std::string::npos);

    REQUIRE(std::filesystem::exists(outDir / "good.txt"));

    // Nothing escaped outDir: no evil.txt beside it, no nested "a" directory
    // or loose "b.txt" anywhere under it, and neither reserved-device name
    // was ever opened for writing.
    CHECK_FALSE(std::filesystem::exists(outDir.parent_path() / "evil.txt"));
    CHECK_FALSE(std::filesystem::exists(outDir / "a"));
    CHECK_FALSE(std::filesystem::exists(outDir / "b.txt"));
    CHECK_FALSE(std::filesystem::exists(outDir / "con.txt"));

    // Only the one safe entry made it to disk.
    size_t count = 0;
    for (const auto& _ : std::filesystem::directory_iterator(outDir)) {
        (void)_;
        ++count;
    }
    CHECK(count == 1);

    std::filesystem::remove_all(outDir);
    std::filesystem::remove(path);
}

TEST_CASE("cli decode text entry prints hello box and returns kOk") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());
    auto path = WriteSampleBox();

    std::ostringstream out;
    int rc = Onyx::Cli::CmdDecode(ws, path, "txt", /*strict=*/false, out);
    CHECK(rc == Onyx::Cli::kOk);
    CHECK(out.str().find("hello box") != std::string::npos);

    std::filesystem::remove(path);
}

TEST_CASE("cli decode strict returns kStrictErrors when the document carries an Error diag") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());
    auto path = WriteSampleBox();

    std::ostringstream out;
    // "txt" itself decodes fine, but the same document also parsed the
    // corrupt "bad" entry, which reported an Error diag -- --strict must
    // surface that even though this particular decode succeeded.
    int rc = Onyx::Cli::CmdDecode(ws, path, "txt", /*strict=*/true, out);
    CHECK(rc == Onyx::Cli::kStrictErrors);

    std::filesystem::remove(path);
}

TEST_CASE("cli decode scene entry prints parts materials vertices summary and returns kOk") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());
    auto path = WriteMeshBox();

    std::ostringstream out;
    int rc = Onyx::Cli::CmdDecode(ws, path, "mesh", /*strict=*/false, out);
    CHECK(rc == Onyx::Cli::kOk);
    // One cube part (24 vertices -- 6 faces * 4 vertices each, see
    // OnyxBoxModule.cpp's BuildCubePart), one material.
    CHECK(out.str().find("scene mesh parts=1 materials=1 vertices=24") != std::string::npos);

    std::filesystem::remove(path);
}

// `decode --to gltf` (T5, M5): CmdDecode's export path, exercised through
// Onyx::Cli::CmdDecode directly (same "through Cli::Commands" shape every
// other case in this file uses) with a real Onyx::Exchange::
// ExportSceneData bound as the SceneExportFn hook -- exactly what
// Examples/OnyxCli/Gltf.cpp's MakeGltfExportFn wires in for the real CLI
// binary (Include/Onyx/Cli/Gltf.h), just constructed inline here since
// onyx_tests links Onyx::Exchange directly and has no reason to go
// through that indirection. Uses the same OnyxBox mesh fixture
// (WriteMeshBox, the `render-fixture` shape Examples/OnyxCli/Main.cpp's
// WriteRenderFixture also builds) as the scene-summary test above --
// static (no skeleton), so this only proves the CLI wiring end to end,
// not skin export (Tests/gltf_test.cpp's GltfExport:* cases own that).
TEST_CASE("cli decode --to gltf exports the mesh entry and returns kOk") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());
    auto path = WriteMeshBox();
    auto outPath = std::filesystem::temp_directory_path() / "onyx_cli_decode_to_gltf.glb";
    std::filesystem::remove(outPath);

    Onyx::Exchange::GltfOptions gltfOpts;
    Onyx::Cli::SceneExportFn exportFn = [gltfOpts](const Onyx::Parsers::SceneData& scene,
                                                     const std::filesystem::path& out,
                                                     std::string& err) {
        return Onyx::Exchange::ExportSceneData(scene, out, gltfOpts, err);
    };

    std::ostringstream out;
    int rc = Onyx::Cli::CmdDecode(ws, path, "mesh", /*strict=*/false, out, /*moduleHint=*/{},
                                   "gltf", outPath, exportFn);
    CHECK(rc == Onyx::Cli::kOk);
    CHECK(out.str().find("exported mesh to") != std::string::npos);
    REQUIRE(std::filesystem::exists(outPath));
    CHECK(std::filesystem::file_size(outPath) > 0);

    std::filesystem::remove(outPath);
    std::filesystem::remove(path);
}

TEST_CASE("cli decode --to gltf with no exporter registered returns kUsage") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());
    auto path = WriteMeshBox();

    std::ostringstream out;
    int rc = Onyx::Cli::CmdDecode(ws, path, "mesh", /*strict=*/false, out, /*moduleHint=*/{},
                                   "gltf", "unused.glb", /*exportFn=*/{});
    CHECK(rc == Onyx::Cli::kUsage);
    CHECK(out.str().find("no exporter available") != std::string::npos);

    std::filesystem::remove(path);
}

TEST_CASE("cli decode --to gltf on a non-Scene entry returns kUsage") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());
    auto path = WriteSampleBox();

    Onyx::Exchange::GltfOptions gltfOpts;
    Onyx::Cli::SceneExportFn exportFn = [gltfOpts](const Onyx::Parsers::SceneData& scene,
                                                     const std::filesystem::path& out,
                                                     std::string& err) {
        return Onyx::Exchange::ExportSceneData(scene, out, gltfOpts, err);
    };

    std::ostringstream out;
    int rc = Onyx::Cli::CmdDecode(ws, path, "txt", /*strict=*/false, out, /*moduleHint=*/{},
                                   "gltf", "unused.glb", exportFn);
    CHECK(rc == Onyx::Cli::kUsage);
    CHECK(out.str().find("no Scene decode capability") != std::string::npos);

    std::filesystem::remove(path);
}

TEST_CASE("cli Run parses --to and --out and forwards them through CmdDecode") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());
    auto path = WriteMeshBox();
    const std::string pathStr = path.string();
    auto outPath = std::filesystem::temp_directory_path() / "onyx_cli_run_to_gltf.glb";
    const std::string outPathStr = outPath.string();
    std::filesystem::remove(outPath);

    Onyx::Exchange::GltfOptions gltfOpts;
    Onyx::Cli::SceneExportFn exportFn = [gltfOpts](const Onyx::Parsers::SceneData& scene,
                                                     const std::filesystem::path& out,
                                                     std::string& err) {
        return Onyx::Exchange::ExportSceneData(scene, out, gltfOpts, err);
    };

    std::ostringstream out, err;
    char argv0[] = "onyxbox-cli";
    char argv1[] = "decode";
    std::vector<char> argv2(pathStr.begin(), pathStr.end());
    argv2.push_back('\0');
    char argv3[] = "mesh";
    char argv4[] = "--to";
    char argv5[] = "gltf";
    char argv6[] = "--out";
    std::vector<char> argv7(outPathStr.begin(), outPathStr.end());
    argv7.push_back('\0');
    char* argv[] = {argv0, argv1, argv2.data(), argv3, argv4, argv5, argv6, argv7.data()};
    int rc = Onyx::Cli::Run(ws, 8, argv, out, err, exportFn);
    CHECK(rc == Onyx::Cli::kOk);
    REQUIRE(std::filesystem::exists(outPath));

    std::filesystem::remove(outPath);
    std::filesystem::remove(path);
}

TEST_CASE("cli decode unknown entry name returns kUsage") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());
    auto path = WriteSampleBox();

    std::ostringstream out;
    int rc = Onyx::Cli::CmdDecode(ws, path, "does-not-exist", /*strict=*/false, out);
    CHECK(rc == Onyx::Cli::kUsage);

    std::filesystem::remove(path);
}

TEST_CASE("cli list returns kNoModule for a file no module accepts") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());
    ws.AddModule(std::make_unique<NeverMatchModule>());
    auto path = WriteGarbageFile();

    std::ostringstream out;
    int rc = Onyx::Cli::CmdList(ws, path, /*json=*/false, out);
    CHECK(rc == Onyx::Cli::kNoModule);

    std::filesystem::remove(path);
}

TEST_CASE("cli probe returns kNoModule when the ranking has no winner") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());
    ws.AddModule(std::make_unique<NeverMatchModule>());
    auto path = WriteGarbageFile();

    std::ostringstream out;
    int rc = Onyx::Cli::CmdProbe(ws, path, out);
    CHECK(rc == Onyx::Cli::kNoModule);
    CHECK(out.str().find("winner: none") != std::string::npos);

    std::filesystem::remove(path);
}

TEST_CASE("cli decode returns kOk when a capability exists but the decoder salvage-fails") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());
    auto path = WriteLyingImageBox();

    std::ostringstream out;
    int rc = Onyx::Cli::CmdDecode(ws, path, "liar", /*strict=*/false, out);
    CHECK(rc == Onyx::Cli::kOk);
    CHECK(out.str().find("obx.image.size-mismatch") != std::string::npos);
    // No "image ..." summary line: the decoder returned null.
    CHECK(out.str().find("image liar") == std::string::npos);

    std::filesystem::remove(path);
}

TEST_CASE("cli Run dispatches probe list extract and decode by name") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());
    auto path = WriteSampleBox();
    const std::string pathStr = path.string();

    {
        std::ostringstream out, err;
        char argv0[] = "onyxbox-cli";
        char argv1[] = "probe";
        std::vector<char> argv2(pathStr.begin(), pathStr.end());
        argv2.push_back('\0');
        char* argv[] = {argv0, argv1, argv2.data()};
        int rc = Onyx::Cli::Run(ws, 3, argv, out, err);
        CHECK(rc == Onyx::Cli::kOk);
        CHECK(out.str().find("winner") != std::string::npos);
    }
    {
        std::ostringstream out, err;
        char argv0[] = "onyxbox-cli";
        char argv1[] = "decode";
        std::vector<char> argv2(pathStr.begin(), pathStr.end());
        argv2.push_back('\0');
        char argv3[] = "txt";
        char* argv[] = {argv0, argv1, argv2.data(), argv3};
        int rc = Onyx::Cli::Run(ws, 4, argv, out, err);
        CHECK(rc == Onyx::Cli::kOk);
        CHECK(out.str().find("hello box") != std::string::npos);
    }
    {
        std::ostringstream out, err;
        char argv0[] = "onyxbox-cli";
        char argv1[] = "list";
        std::vector<char> argv2(pathStr.begin(), pathStr.end());
        argv2.push_back('\0');
        char argv3[] = "--json";
        char* argv[] = {argv0, argv1, argv2.data(), argv3};
        int rc = Onyx::Cli::Run(ws, 4, argv, out, err);
        CHECK(rc == Onyx::Cli::kOk);
        CHECK(out.str().find("\"type\":\"obx.image\"") != std::string::npos);
    }

    std::filesystem::remove(path);
}

TEST_CASE("cli Run --game hint overrides ranking to pick the correct module") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    // DecoyModule registered FIRST -- registration order must not be what
    // decides this; only the hint should.
    ws.AddModule(std::make_unique<DecoyModule>());
    ws.AddModule(MakeModule());
    auto path = WriteSampleBox();
    const std::string pathStr = path.string();

    {
        // Without --game: the decoy outranks the real obx module
        // (99 > 95), so list picks the decoy's empty parse.
        std::ostringstream out, err;
        char argv0[] = "onyxbox-cli";
        char argv1[] = "list";
        std::vector<char> argv2(pathStr.begin(), pathStr.end());
        argv2.push_back('\0');
        char argv3[] = "--json";
        char* argv[] = {argv0, argv1, argv2.data(), argv3};
        int rc = Onyx::Cli::Run(ws, 4, argv, out, err);
        CHECK(rc == Onyx::Cli::kOk);
        CHECK(out.str().find("\"module\":\"decoy\"") != std::string::npos);
        CHECK(out.str().find("\"entries\":[]") != std::string::npos);
    }
    {
        // With --game obx: the hint wins outright over ranking (see
        // Workspace::PrepareDocument), forcing the real OnyxBox module --
        // its full tree comes back instead of the decoy's empty one.
        std::ostringstream out, err;
        char argv0[] = "onyxbox-cli";
        char argv1[] = "list";
        std::vector<char> argv2(pathStr.begin(), pathStr.end());
        argv2.push_back('\0');
        char argv3[] = "--json";
        char argv4[] = "--game";
        char argv5[] = "obx";
        char* argv[] = {argv0, argv1, argv2.data(), argv3, argv4, argv5};
        int rc = Onyx::Cli::Run(ws, 6, argv, out, err);
        CHECK(rc == Onyx::Cli::kOk);
        CHECK(out.str().find("\"module\":\"obx\"") != std::string::npos);
        CHECK(out.str().find("\"type\":\"obx.image\"") != std::string::npos);
    }

    std::filesystem::remove(path);
}

TEST_CASE("cli parse keeps a 0xFFFFFFFF declared size un-truncated in source.size and flags the entry Failed") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());
    auto path = WriteHugeSizeBox();

    Onyx::Modules::DocumentId id = ws.Open(path);
    REQUIRE(id != 0);
    Onyx::Modules::Document* doc = ws.Get(id);
    REQUIRE(doc != nullptr);
    REQUIRE(doc->roots.size() == 1);

    const Onyx::Domain::AssetEntry& e = doc->roots[0];
    CHECK(e.source.size == 0xFFFFFFFFull);
    CHECK((static_cast<uint8_t>(e.flags) & static_cast<uint8_t>(Onyx::Domain::NodeFlags::Failed)) != 0);

    ws.Close(id);
    std::filesystem::remove(path);
}

TEST_CASE("cli Run returns kUsage to err for a junk command") {
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(MakeModule());

    std::ostringstream out, err;
    char argv0[] = "onyxbox-cli";
    char argv1[] = "frobnicate";
    char* argv[] = {argv0, argv1};
    int rc = Onyx::Cli::Run(ws, 2, argv, out, err);
    CHECK(rc == Onyx::Cli::kUsage);
    CHECK_FALSE(err.str().empty());
    CHECK(out.str().empty());
}
