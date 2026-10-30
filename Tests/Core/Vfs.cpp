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

namespace {

constexpr uint32_t kCount = 10000;

bool ReadInterleaved(std::shared_ptr<const IFile> file, uint32_t parity) {
    Reader reader(std::move(file));
    for (uint32_t i = parity; i < kCount; i += 2) {
        reader.Seek(uint64_t(i) * sizeof(uint32_t));
        auto v = reader.Read<uint32_t>();
        if (!v || *v != i) return false;
    }
    return true;
}

std::vector<std::byte> Sequence() {
    std::vector<std::byte> data(kCount * sizeof(uint32_t));
    for (uint32_t i = 0; i < kCount; ++i) {
        std::memcpy(data.data() + i * sizeof(uint32_t), &i, sizeof(i));
    }
    return data;
}

}

TEST_CASE("Vfs.TwoReadersOverOneFileFromTwoThreads") {
    std::shared_ptr<const IFile> file = std::make_shared<MemoryFile>(Sequence());
    bool okA = false;
    bool okB = false;
    std::thread a([&] { okA = ReadInterleaved(file, 0); });
    std::thread b([&] { okB = ReadInterleaved(file, 1); });
    a.join();
    b.join();
    CHECK(okA);
    CHECK(okB);
}

TEST_CASE("Vfs.TwoReadersOverOneOsFileFromTwoThreads") {
    std::filesystem::path path = std::filesystem::temp_directory_path() / "onyx_two_readers.bin";
    {
        const auto data = Sequence();
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    {
        auto result = OsFile::Open(path);
        REQUIRE(result.ok());
        std::shared_ptr<const IFile> file = result.value();
        bool okA = false;
        bool okB = false;
        std::thread a([&] { okA = ReadInterleaved(file, 0); });
        std::thread b([&] { okB = ReadInterleaved(file, 1); });
        a.join();
        b.join();
        CHECK(okA);
        CHECK(okB);
    }
    std::filesystem::remove(path);
}

TEST_CASE("Vfs.OsFileOpensNonAsciiPath") {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        std::filesystem::path(std::u8string(u8"açãoランス.bin"));
    {
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out);
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
