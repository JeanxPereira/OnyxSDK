#include <doctest/doctest.h>

#include <Onyx/Core/Vfs/IFile.h>
#include <Onyx/Core/Vfs/IVirtualFileSystem.h>
#include <Onyx/Core/Vfs/MemoryFile.h>
#include <Onyx/Core/Vfs/OsFile.h>
#include <Onyx/Core/Vfs/Reader.h>
#include <Onyx/Core/Vfs/SliceFile.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace Onyx::Vfs;

namespace {

std::vector<std::byte> Bytes(std::initializer_list<unsigned> values) {
    std::vector<std::byte> out;
    for (unsigned v : values) out.push_back(static_cast<std::byte>(v));
    return out;
}

}

TEST_CASE("Vfs.MemoryFileReadAtClampsAtEnd") {
    MemoryFile file(std::vector<std::byte>(10, std::byte{7}));
    std::byte buf[8] = {};
    CHECK(file.Size() == 10);
    CHECK(file.ReadAt(6, buf) == 4);
    CHECK(file.ReadAt(10, buf) == 0);
    CHECK(file.ReadAt(50, buf) == 0);
}

TEST_CASE("Vfs.ReaderTypedReadLittleEndian") {
    auto file = std::make_shared<MemoryFile>(Bytes({1, 2, 0, 0}));
    Reader reader(file);
    auto v = reader.Read<uint32_t>();
    REQUIRE(v.has_value());
    CHECK(*v == 0x201);
    CHECK(reader.Eof());
}

TEST_CASE("Vfs.ReaderBigEndian") {
    auto file = std::make_shared<MemoryFile>(Bytes({0, 0, 2, 1, 0x12, 0x34}));
    Reader reader(file);
    reader.SetBigEndian(true);
    auto a = reader.Read<uint32_t>();
    auto b = reader.Read<uint16_t>();
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(*a == 0x201);
    CHECK(*b == 0x1234);
}

TEST_CASE("Vfs.ReaderShortReadReturnsNullopt") {
    auto file = std::make_shared<MemoryFile>(Bytes({1, 2, 3}));
    Reader reader(file);
    CHECK_FALSE(reader.Read<uint32_t>().has_value());
    CHECK(reader.Tell() == 0);
}

TEST_CASE("Vfs.SliceFileClampsToParent") {
    auto parent = std::make_shared<MemoryFile>(std::vector<std::byte>(10, std::byte{1}));
    SliceFile slice(parent, 8, 100);
    CHECK(slice.Size() == 2);
    std::byte buf[8] = {};
    CHECK(slice.ReadAt(0, buf) == 2);
    CHECK(slice.ReadAt(2, buf) == 0);
}

TEST_CASE("Vfs.TwoReadersOverOneFileFromTwoThreads") {
    constexpr uint32_t kCount = 10000;
    std::vector<std::byte> data(kCount * sizeof(uint32_t));
    for (uint32_t i = 0; i < kCount; ++i) {
        std::memcpy(data.data() + i * sizeof(uint32_t), &i, sizeof(i));
    }
    std::shared_ptr<const IFile> file = std::make_shared<MemoryFile>(std::move(data));
    constexpr uint64_t half = (kCount / 2) * sizeof(uint32_t);

    bool okA = true;
    bool okB = true;
    auto run = [&](uint64_t base, uint32_t first, bool& ok) {
        Reader reader(file, base, half);
        for (uint32_t i = 0; i < kCount / 2; ++i) {
            auto v = reader.Read<uint32_t>();
            if (!v || *v != first + i) ok = false;
        }
    };
    std::thread a(run, 0, 0u, std::ref(okA));
    std::thread b(run, half, kCount / 2, std::ref(okB));
    a.join();
    b.join();
    CHECK(okA);
    CHECK(okB);
}

TEST_CASE("Vfs.OsFileOpensNonAsciiPath") {
    std::filesystem::path path = std::filesystem::temp_directory_path() / L"açãoランス.bin";
    {
        std::ofstream out(path, std::ios::binary);
        out << "hello";
    }
    {
        auto result = OsFile::Open(path);
        REQUIRE(result.ok());
        auto file = result.value();
        CHECK(file->Size() == 5);
        char buf[5] = {};
        CHECK(file->ReadAt(0, std::as_writable_bytes(std::span(buf))) == 5);
        CHECK(std::string(buf, 5) == "hello");
    }
    std::filesystem::remove(path);
}

TEST_CASE("Vfs.OsFileMissingPathFails") {
    auto result = OsFile::Open(std::filesystem::temp_directory_path() / "onyx_missing_file_xyz.bin");
    CHECK_FALSE(result.ok());
    CHECK(result.error().code == "vfs.open");
}
