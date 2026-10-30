#include <doctest/doctest.h>

#include <Onyx/Core/Vfs/IsoFileSystem.h>
#include <Onyx/Core/Vfs/MemoryFile.h>
#include <Onyx/Core/Vfs/Reader.h>
#include <Onyx/TestKit/IsoBuilder.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace Onyx::Vfs;
using Onyx::TestKit::Corruption;
using Onyx::TestKit::IsoBuilder;

namespace {

std::vector<std::byte> Payload(size_t n, unsigned seed) {
    std::vector<std::byte> bytes(n);
    for (size_t i = 0; i < n; ++i) bytes[i] = static_cast<std::byte>((i * 7 + seed) & 0xFF);
    return bytes;
}

std::shared_ptr<const IFile> AsFile(std::vector<std::byte> image) {
    return std::make_shared<MemoryFile>(std::move(image));
}

bool RunWithWatchdog(std::function<bool()> work) {
    auto promise = std::make_shared<std::promise<bool>>();
    auto future = promise->get_future();
    std::thread([promise, work = std::move(work)] { promise->set_value(work()); }).detach();
    if (future.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
        FAIL("operation did not finish within 2 seconds");
    }
    return future.get();
}

size_t CountRecursive(const IsoFileSystem& fs, const std::string& dir) {
    size_t count = 0;
    for (const auto& entry : fs.List(dir)) {
        ++count;
        if (entry.isDirectory) count += CountRecursive(fs, (dir == "/" ? "" : dir) + "/" + entry.name);
    }
    return count;
}

bool HasName(const std::vector<DirEntry>& entries, const std::string& name, bool directory) {
    return std::any_of(entries.begin(), entries.end(), [&](const DirEntry& e) {
        return e.name == name && e.isDirectory == directory;
    });
}

}

TEST_CASE("Iso.ListsRootEntries") {
    IsoBuilder builder;
    builder.AddFile("readme.txt", Payload(100, 1));
    builder.AddFile("data.bin", Payload(5000, 2));
    builder.AddDirectory("sub");
    auto mounted = IsoFileSystem::Mount(AsFile(builder.Build()));
    REQUIRE(mounted.ok());
    const auto entries = mounted.value()->List("/");
    CHECK(entries.size() == 3);
    CHECK(HasName(entries, "README.TXT", false));
    CHECK(HasName(entries, "DATA.BIN", false));
    CHECK(HasName(entries, "SUB", true));
    const auto data = std::find_if(entries.begin(), entries.end(),
                                   [](const DirEntry& e) { return e.name == "DATA.BIN"; });
    CHECK(data->size == 5000);
}

TEST_CASE("Iso.OpensNestedFileAndReadsBytes") {
    const auto payload = Payload(3000, 9);
    IsoBuilder builder;
    builder.AddFile("a/b/deep.bin", payload);
    auto mounted = IsoFileSystem::Mount(AsFile(builder.Build()));
    REQUIRE(mounted.ok());
    auto file = mounted.value()->Open("/A/B/DEEP.BIN;1");
    REQUIRE(file);
    CHECK(file->Size() == payload.size());
    Reader reader(file);
    CHECK(reader.ReadAll() == payload);
    CHECK(!mounted.value()->Open("/A/B"));
    CHECK(!mounted.value()->Open("/missing"));
}

TEST_CASE("Iso.ExistsIsCaseInsensitive") {
    IsoBuilder builder;
    builder.AddFile("Dir/File.Dat", Payload(10, 3));
    auto mounted = IsoFileSystem::Mount(AsFile(builder.Build()));
    REQUIRE(mounted.ok());
    CHECK(mounted.value()->Exists("/dir/file.dat"));
    CHECK(mounted.value()->Exists("DIR/FILE.DAT;1"));
    CHECK(mounted.value()->Exists("/Dir"));
    CHECK(!mounted.value()->Exists("/dir/other.dat"));
}

TEST_CASE("Iso.RootSizePastEofReturnsErrorNotHang") {
    IsoBuilder builder;
    builder.AddFile("a.bin", Payload(10, 1));
    auto image = AsFile(builder.BuildCorrupt(Corruption::RootSizePastEof));
    CHECK(RunWithWatchdog([image] {
        auto mounted = IsoFileSystem::Mount(image);
        return !mounted.ok() || !mounted.value()->Warnings().empty();
    }));
}

TEST_CASE("Iso.RecordLenBelow34StopsDirectoryWithWarning") {
    IsoBuilder builder;
    builder.AddFile("a.bin", Payload(10, 1));
    auto mounted = IsoFileSystem::Mount(AsFile(builder.BuildCorrupt(Corruption::RecordLenBelow34)));
    REQUIRE(mounted.ok());
    CHECK(!mounted.value()->Warnings().empty());
    CHECK(mounted.value()->List("/").empty());
}

TEST_CASE("Iso.SelfPointingExtentDoesNotRecurse") {
    IsoBuilder builder;
    builder.AddDirectory("sub");
    builder.AddFile("keep.bin", Payload(10, 1));
    auto mounted = IsoFileSystem::Mount(AsFile(builder.BuildCorrupt(Corruption::SelfPointingExtent)));
    REQUIRE(mounted.ok());
    CHECK(!mounted.value()->Warnings().empty());
    CHECK(mounted.value()->Exists("/KEEP.BIN"));
}

TEST_CASE("Iso.SharedExtentsDoNotAmplify") {
    IsoBuilder builder;
    auto image = AsFile(builder.BuildCorrupt(Corruption::SharedExtentFanOut));
    size_t walked = 0;
    bool warned = false;
    CHECK(RunWithWatchdog([&, image] {
        auto mounted = IsoFileSystem::Mount(image);
        if (!mounted.ok()) return false;
        CountRecursive(*mounted.value(), "/");
        walked = mounted.value()->DirectoriesWalked();
        for (const auto& w : mounted.value()->Warnings()) warned = warned || w.code == "iso.walk.duplicateExtent";
        return true;
    }));
    CHECK(walked <= 16);
    CHECK(warned);
}
