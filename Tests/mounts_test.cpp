#include <doctest/doctest.h>

#include <Onyx/Cli/Commands.h>
#include <Onyx/Domain/Entry.h>
#include <Onyx/Modules/Workspace.h>
#include <Onyx/Types/TypeCatalog.h>
#include <Onyx/Vfs/IFile.h>
#include <Onyx/Vfs/IVirtualFileSystem.h>

#include <cstdint>
#include <cstdio>       // SEEK_SET, SEEK_CUR, SEEK_END
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace Onyx::Modules;

namespace {

std::filesystem::path write_temp_file(const std::string& name, const std::string& contents) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream f(path, std::ios::binary);
    f << contents;
    f.close();
    return path;
}

// In-memory IFile over a fixed byte buffer -- stands in for an inner file a
// mounted VFS hands back, so these tests never need a real archive format.
class MemFile : public Onyx::Vfs::IFile {
public:
    explicit MemFile(std::vector<uint8_t> bytes) : m_bytes(std::move(bytes)) {}

    size_t Read(void* dest, size_t bytes) override {
        size_t avail = m_pos < int64_t(m_bytes.size()) ? m_bytes.size() - size_t(m_pos) : 0;
        size_t n = bytes < avail ? bytes : avail;
        if (n > 0) std::memcpy(dest, m_bytes.data() + m_pos, n);
        m_pos += int64_t(n);
        return n;
    }
    bool Seek(int64_t offset, int origin) override {
        int64_t base = 0;
        if (origin == SEEK_CUR) base = m_pos;
        else if (origin == SEEK_END) base = int64_t(m_bytes.size());
        int64_t next = base + offset;
        if (next < 0 || next > int64_t(m_bytes.size())) return false;
        m_pos = next;
        return true;
    }
    int64_t Tell() override { return m_pos; }
    size_t Size() const override { return m_bytes.size(); }
    bool IsEOF() const override { return m_pos >= int64_t(m_bytes.size()); }
    bool IsValid() const override { return true; }

private:
    std::vector<uint8_t> m_bytes;
    int64_t m_pos = 0;
};

// In-memory VFS: OpenFile("inner") returns a MemFile with fixed content
// distinct from anything the root container file holds; any other path
// returns null. Good enough to stand in for a real mount -- the module
// under test only ever asks for "inner".
class MemVfs : public Onyx::Vfs::IVirtualFileSystem {
public:
    bool IsValid() const override { return true; }
    std::vector<std::string> ListDirectory(const std::string&) override { return {"inner"}; }
    std::unique_ptr<Onyx::Vfs::IFile> OpenFile(const std::string& path) override {
        if (path != "inner") return nullptr;
        static const std::string kPayload = "INNER-BYTES";
        return std::make_unique<MemFile>(std::vector<uint8_t>(kPayload.begin(), kPayload.end()));
    }
    bool Exists(const std::string& path) override { return path == "inner"; }
};

// Declares a MountSpec for extension "pak" backed by MemVfs. Records what
// ContainerContext handed ParseContainer (mountedVfs) so tests can assert
// on Workspace's mount-at-open behavior directly. When a mount succeeded,
// it also opens "inner" through it, pushes the result into the file table,
// and stamps a child entry's fileIndex with the resulting slot -- exactly
// the "module parsing through a mount" contract Task 7 adds. It always
// also emits a second child entry pointing at fileIndex 99, which never
// exists in the table, to exercise extract's out-of-range salvage path.
struct MountFake : Onyx::Modules::IGameModule {
    // When true, the MountSpec's factory always returns nullptr (a refused
    // mount) regardless of extension match.
    bool refuseMount = false;

    bool sawParseContainer = false;
    const Onyx::Vfs::IVirtualFileSystem* seenMountedVfs = nullptr;

    Onyx::Modules::ModuleInfo Info() const override {
        return Onyx::Modules::ModuleInfo{"mountfake", "MountFake", {}, {}};
    }

    Onyx::Modules::ProbeResult Probe(const Onyx::Modules::ProbeInput&) const override {
        return Onyx::Modules::ProbeResult{90, "always"};
    }

    void RegisterTypes(Onyx::Types::TypeRegistrar&) override {}
    void RegisterDecoders(Onyx::Modules::DecoderRegistry&) override {}

    std::vector<Onyx::Modules::MountSpec> Mounts() const override {
        Onyx::Modules::MountSpec spec;
        spec.label = "MemPak";
        spec.extensions = {"pak"};
        bool refuse = refuseMount;
        spec.mount = [refuse](const std::filesystem::path&)
                -> std::shared_ptr<Onyx::Vfs::IVirtualFileSystem> {
            if (refuse) return nullptr;
            return std::make_shared<MemVfs>();
        };
        return {spec};
    }

    Onyx::Modules::ParseResult ParseContainer(Onyx::Modules::ContainerContext& ctx) override {
        sawParseContainer = true;
        seenMountedVfs = ctx.mountedVfs;

        Onyx::Domain::AssetEntry root;
        root.name = "root";

        if (ctx.mountedVfs && ctx.fileTable) {
            auto inner = ctx.mountedVfs->OpenFile("inner");
            if (inner) {
                Onyx::Domain::AssetEntry child;
                child.name = "inner.bin";
                child.source.fileIndex = uint32_t(ctx.fileTable->size());
                child.source.size = uint64_t(inner->Size());
                ctx.fileTable->push_back(
                    std::shared_ptr<Onyx::Vfs::IFile>(std::move(inner)));
                root.children.push_back(std::move(child));
            }
        }

        // A bogus entry pointing at a file index that will never exist,
        // alongside whatever real entry was pushed above -- extract must
        // skip just this one and keep going.
        Onyx::Domain::AssetEntry bogus;
        bogus.name = "ghost.bin";
        bogus.source.fileIndex = 99;
        bogus.source.size = 4;
        root.children.push_back(std::move(bogus));

        ctx.roots.push_back(std::move(root));
        return Onyx::Modules::ParseResult{true};
    }
};

} // namespace

TEST_CASE("MountSpec: extension match mounts, mismatch leaves mountedVfs null") {
    auto pakPath = write_temp_file("onyx_mounts_test_match.pak", "root file bytes");
    auto obxPath = write_temp_file("onyx_mounts_test_mismatch.obx", "root file bytes");

    {
        Workspace ws(Onyx::Types::TypeCatalog::Get());
        auto mod = std::make_unique<MountFake>();
        MountFake* raw = mod.get();
        ws.AddModule(std::move(mod));

        auto id = ws.Open(pakPath);
        REQUIRE(id != 0);
        CHECK(raw->sawParseContainer);
        CHECK(raw->seenMountedVfs != nullptr);

        auto* doc = ws.Get(id);
        REQUIRE(doc);
        CHECK(doc->mountedVfs != nullptr);
    } // ~Workspace closes the OsFile before we try to remove it.

    {
        Workspace ws(Onyx::Types::TypeCatalog::Get());
        auto mod = std::make_unique<MountFake>();
        MountFake* raw = mod.get();
        ws.AddModule(std::move(mod));

        auto id = ws.Open(obxPath);
        REQUIRE(id != 0);
        CHECK(raw->sawParseContainer);
        CHECK(raw->seenMountedVfs == nullptr);   // extension "obx" matches no MountSpec

        auto* doc = ws.Get(id);
        REQUIRE(doc);
        CHECK(doc->mountedVfs == nullptr);
    }

    std::filesystem::remove(pakPath);
    std::filesystem::remove(obxPath);
}

TEST_CASE("MountSpec: a refused mount falls through to a flat-file parse with exactly one Warning diag") {
    auto path = write_temp_file("onyx_mounts_test_refused.pak", "root file bytes");
    {
        Workspace ws(Onyx::Types::TypeCatalog::Get());
        auto mod = std::make_unique<MountFake>();
        mod->refuseMount = true;
        ws.AddModule(std::move(mod));

        auto id = ws.Open(path);
        REQUIRE(id != 0);

        auto* doc = ws.Get(id);
        REQUIRE(doc);
        CHECK(doc->ready);
        CHECK(doc->mountedVfs == nullptr);
        REQUIRE(doc->roots.size() == 1);   // parse proceeded despite the refusal

        auto diags = doc->diags.Drain();
        int warnings = 0;
        for (auto& d : diags) {
            if (d.severity == Onyx::Services::Severity::Warning) {
                ++warnings;
                CHECK(d.message == "mount refused, parsing as flat file");
            }
        }
        CHECK(warnings == 1);
    }
    std::filesystem::remove(path);
}

TEST_CASE("Extract: fileIndex 1 reads bytes from fileTable[1], not the root container file") {
    auto path = write_temp_file("onyx_mounts_test_extract.pak", "ROOT-FILE-CONTENT");
    auto outDir = std::filesystem::temp_directory_path() / "onyx_mounts_extract_out";
    std::filesystem::remove_all(outDir);

    Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(std::make_unique<MountFake>());

    std::ostringstream out;
    int rc = Onyx::Cli::CmdExtract(ws, path, outDir, out);
    CHECK(rc == Onyx::Cli::kOk);

    REQUIRE(std::filesystem::exists(outDir / "inner.bin"));
    std::string content;
    {
        // Scoped so the handle closes before remove_all below -- Windows
        // refuses to delete a directory containing a file still open for
        // read.
        std::ifstream f(outDir / "inner.bin", std::ios::binary);
        content.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    CHECK(content == "INNER-BYTES");   // from fileTable[1] (the mount), not "ROOT-FILE-CONTENT"

    std::filesystem::remove_all(outDir);
    std::filesystem::remove(path);
}

TEST_CASE("Extract: an out-of-range fileIndex is skipped with an error line, other entries still extracted") {
    auto path = write_temp_file("onyx_mounts_test_extract_oob.pak", "ROOT-FILE-CONTENT");
    auto outDir = std::filesystem::temp_directory_path() / "onyx_mounts_extract_oob_out";
    std::filesystem::remove_all(outDir);

    Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(std::make_unique<MountFake>());

    std::ostringstream out;
    int rc = Onyx::Cli::CmdExtract(ws, path, outDir, out);
    CHECK(rc == Onyx::Cli::kOk);

    const std::string text = out.str();
    CHECK(text.find("error 'ghost.bin': file index 99 out of range") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(outDir / "ghost.bin"));

    // The other, valid entry (fileIndex 1) was still extracted despite the
    // bogus one -- a salvage failure on one entry never aborts the extract.
    REQUIRE(std::filesystem::exists(outDir / "inner.bin"));

    std::filesystem::remove_all(outDir);
    std::filesystem::remove(path);
}
