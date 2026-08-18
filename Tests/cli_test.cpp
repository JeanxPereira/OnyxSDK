#include <doctest/doctest.h>

#include <Onyx/Cli/Commands.h>
#include <Onyx/Domain/Entry.h>
#include <Onyx/Modules/Workspace.h>
#include <Onyx/Types/TypeCatalog.h>

#include <OnyxBoxModule.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

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

// Three entries, all with valid in-bounds payload ranges (so ParseContainer
// never flags any of them Failed), but two carry hostile names: "../evil.txt"
// (parent-directory escape) and "a/b.txt" (nested path). CmdExtract must
// refuse to write either -- only "good.txt" is a safe bare filename.
std::filesystem::path WriteUnsafeNamesBox() {
    const std::string safeText = "safe data";
    const std::string evilText = "evil data";
    const std::string nestedText = "nested data";

    const std::string goodName = "good.txt";
    const std::string evilName = "../evil.txt";
    const std::string nestedName = "a/b.txt";

    // magic(4) + count(4) + 3 * (nameLen(2) + name + kind(1) + offset(4) + size(4))
    const uint32_t headerSize =
        4 + 4 + uint32_t(3 * (2 + 1 + 4 + 4)) +
        uint32_t(goodName.size() + evilName.size() + nestedName.size());

    const uint32_t safeOffset = headerSize;
    const uint32_t safeSize = uint32_t(safeText.size());
    const uint32_t evilOffset = safeOffset + safeSize;
    const uint32_t evilSize = uint32_t(evilText.size());
    const uint32_t nestedOffset = evilOffset + evilSize;
    const uint32_t nestedSize = uint32_t(nestedText.size());

    std::vector<uint8_t> buf;
    buf.insert(buf.end(), {uint8_t('O'), uint8_t('B'), uint8_t('X'), uint8_t('1')});
    PutU32LE(buf, 3);

    PutTocHeader(buf, goodName, 2, safeOffset, safeSize);
    PutTocHeader(buf, evilName, 2, evilOffset, evilSize);
    PutTocHeader(buf, nestedName, 2, nestedOffset, nestedSize);

    buf.insert(buf.end(), safeText.begin(), safeText.end());
    buf.insert(buf.end(), evilText.begin(), evilText.end());
    buf.insert(buf.end(), nestedText.begin(), nestedText.end());

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

std::unique_ptr<OnyxBox::OnyxBoxModule> MakeModule() {
    return std::make_unique<OnyxBox::OnyxBoxModule>();
}

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

    REQUIRE(std::filesystem::exists(outDir / "good.txt"));

    // Nothing escaped outDir: no evil.txt beside it, no nested "a" directory
    // or loose "b.txt" anywhere under it.
    CHECK_FALSE(std::filesystem::exists(outDir.parent_path() / "evil.txt"));
    CHECK_FALSE(std::filesystem::exists(outDir / "a"));
    CHECK_FALSE(std::filesystem::exists(outDir / "b.txt"));

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
    auto path = WriteGarbageFile();

    std::ostringstream out;
    int rc = Onyx::Cli::CmdList(ws, path, /*json=*/false, out);
    CHECK(rc == Onyx::Cli::kNoModule);

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
