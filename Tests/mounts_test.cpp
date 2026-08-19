#include <doctest/doctest.h>

#include <Onyx/Cli/Commands.h>
#include <Onyx/Domain/Entry.h>
#include <Onyx/Modules/Workspace.h>
#include <Onyx/Types/TypeCatalog.h>
#include <Onyx/Vfs/IFile.h>
#include <Onyx/Vfs/IVirtualFileSystem.h>

#include <OnyxBoxModule.h>

#include <chrono>
#include <cstdint>
#include <cstdio>       // SEEK_SET, SEEK_CUR, SEEK_END
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
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

// ── OBXPAK end-to-end fixture builders (Task 8) ─────────────────────────────
// Local to this file, in the same spirit as cli_test.cpp's WriteSampleBox
// helpers: each test file keeps its own fixture builders so the TEST_CASE
// sets stay independent.

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

// Builds one OBX1 container's raw bytes from a list of (name, kind,
// payload) TOC entries, laid out back-to-back exactly as OnyxBoxModule
// expects. When `corruptLast` is true, the LAST entry's declared
// payloadOffset is pushed far past EOF and its payload bytes are never
// actually appended (mirrors cli_test.cpp's WriteSampleBox "bad" entry) --
// every earlier entry's offset is unaffected since nothing before it moved.
std::vector<uint8_t> BuildObx(
        const std::vector<std::tuple<std::string, uint8_t, std::string>>& entries,
        bool corruptLast = false) {
    uint32_t headerSize = 4 + 4;
    for (const auto& [name, kind, payload] : entries) {
        (void)kind; (void)payload;
        headerSize += uint32_t(2 + name.size() + 1 + 4 + 4);
    }

    std::vector<uint8_t> buf;
    buf.insert(buf.end(), {uint8_t('O'), uint8_t('B'), uint8_t('X'), uint8_t('1')});
    PutU32LE(buf, uint32_t(entries.size()));

    uint32_t cursor = headerSize;
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& [name, kind, payload] = entries[i];
        const bool corrupt = corruptLast && i + 1 == entries.size();
        const uint32_t off = corrupt ? headerSize + 1000000u : cursor;
        PutU16LE(buf, uint16_t(name.size()));
        buf.insert(buf.end(), name.begin(), name.end());
        buf.push_back(kind);
        PutU32LE(buf, off);
        PutU32LE(buf, uint32_t(payload.size()));
        if (!corrupt) cursor += uint32_t(payload.size());
    }
    for (size_t i = 0; i < entries.size(); ++i) {
        const bool corrupt = corruptLast && i + 1 == entries.size();
        if (corrupt) continue;   // its declared offset points past EOF -- no bytes to write
        const auto& payload = std::get<2>(entries[i]);
        buf.insert(buf.end(), payload.begin(), payload.end());
    }
    return buf;
}

// Builds an OBXPAK's raw bytes: magic "OBP1", u32 count, then per file
// u32 nameLen | name | u32 size | raw bytes of a complete OBX1 container.
std::vector<uint8_t> BuildObxPak(
        const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
    std::vector<uint8_t> buf;
    buf.insert(buf.end(), {uint8_t('O'), uint8_t('B'), uint8_t('P'), uint8_t('1')});
    PutU32LE(buf, uint32_t(files.size()));
    for (const auto& [name, bytes] : files) {
        PutU32LE(buf, uint32_t(name.size()));
        buf.insert(buf.end(), name.begin(), name.end());
        PutU32LE(buf, uint32_t(bytes.size()));
        buf.insert(buf.end(), bytes.begin(), bytes.end());
    }
    return buf;
}

std::filesystem::path write_temp_bytes(const std::string& name, const std::vector<uint8_t>& bytes) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    f.close();
    return path;
}

std::string read_file_string(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
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

// In-memory IFile whose Read() always throws -- stands in for a hostile or
// buggy mounted VFS implementation. ExtractEntries (Source/Cli/Commands.cpp)
// runs module-supplied IFile code (Seek/Read) for every fileIndex >= 1
// entry; that call must be contained at the module boundary (spec §7.1),
// salvaging just this one entry, never letting the exception reach
// CmdExtract's caller.
class ThrowingReadFile : public Onyx::Vfs::IFile {
public:
    size_t Read(void*, size_t) override { throw std::runtime_error("Read() boom"); }
    bool Seek(int64_t, int) override { return true; }
    int64_t Tell() override { return 0; }
    size_t Size() const override { return 4; }
    bool IsEOF() const override { return false; }
    bool IsValid() const override { return true; }
};

// VFS handing back one normal file ("good") and one whose Read() throws
// ("bad") -- backs MountThrowingReadFake below.
class ThrowingReadVfs : public Onyx::Vfs::IVirtualFileSystem {
public:
    bool IsValid() const override { return true; }
    std::vector<std::string> ListDirectory(const std::string&) override { return {"good", "bad"}; }
    std::unique_ptr<Onyx::Vfs::IFile> OpenFile(const std::string& path) override {
        if (path == "good") {
            static const std::string kPayload = "GOOD-BYTES";
            return std::make_unique<MemFile>(std::vector<uint8_t>(kPayload.begin(), kPayload.end()));
        }
        if (path == "bad") return std::make_unique<ThrowingReadFile>();
        return nullptr;
    }
    bool Exists(const std::string& path) override { return path == "good" || path == "bad"; }
};

// Mounts ThrowingReadVfs and pushes both "good" (fileIndex 1) and "bad"
// (fileIndex 2) into the file table with entries pointing at each --
// exercises extract salvaging a throwing mounted-file read alongside a
// normal one, same shape as MountFake's out-of-range case above.
struct MountThrowingReadFake : Onyx::Modules::IGameModule {
    Onyx::Modules::ModuleInfo Info() const override {
        return Onyx::Modules::ModuleInfo{"mountthrowread", "MountThrowingRead", {}, {}};
    }

    Onyx::Modules::ProbeResult Probe(const Onyx::Modules::ProbeInput&) const override {
        return Onyx::Modules::ProbeResult{90, "always"};
    }

    void RegisterTypes(Onyx::Types::TypeRegistrar&) override {}
    void RegisterDecoders(Onyx::Modules::DecoderRegistry&) override {}

    std::vector<Onyx::Modules::MountSpec> Mounts() const override {
        Onyx::Modules::MountSpec spec;
        spec.label = "ThrowPak";
        spec.extensions = {"pak"};
        spec.mount = [](const std::filesystem::path&)
                -> std::shared_ptr<Onyx::Vfs::IVirtualFileSystem> {
            return std::make_shared<ThrowingReadVfs>();
        };
        return {spec};
    }

    Onyx::Modules::ParseResult ParseContainer(Onyx::Modules::ContainerContext& ctx) override {
        Onyx::Domain::AssetEntry root;
        root.name = "root";

        if (ctx.mountedVfs && ctx.fileTable) {
            for (const char* name : {"good", "bad"}) {
                auto inner = ctx.mountedVfs->OpenFile(name);
                if (!inner) continue;
                Onyx::Domain::AssetEntry child;
                child.name = std::string(name) + ".bin";
                child.source.fileIndex = uint32_t(ctx.fileTable->size());
                child.source.size = uint64_t(inner->Size());
                ctx.fileTable->push_back(std::shared_ptr<Onyx::Vfs::IFile>(std::move(inner)));
                root.children.push_back(std::move(child));
            }
        }

        ctx.roots.push_back(std::move(root));
        return Onyx::Modules::ParseResult{true};
    }
};

// Fix round 1: Mounts() throwing must be contained the same way a throwing
// ParseContainer already is -- an Error diag, then a flat-file parse, never
// an exception escaping Open()/OpenAsync().
struct MountsThrowFake : Onyx::Modules::IGameModule {
    Onyx::Modules::ModuleInfo Info() const override {
        return Onyx::Modules::ModuleInfo{"mountsthrow", "MountsThrow", {}, {}};
    }

    Onyx::Modules::ProbeResult Probe(const Onyx::Modules::ProbeInput&) const override {
        return Onyx::Modules::ProbeResult{90, "always"};
    }

    void RegisterTypes(Onyx::Types::TypeRegistrar&) override {}
    void RegisterDecoders(Onyx::Modules::DecoderRegistry&) override {}

    std::vector<Onyx::Modules::MountSpec> Mounts() const override {
        throw std::runtime_error("Mounts() boom");
    }

    Onyx::Modules::ParseResult ParseContainer(Onyx::Modules::ContainerContext& ctx) override {
        Onyx::Domain::AssetEntry e;
        e.name = "flat";
        ctx.roots.push_back(std::move(e));
        return Onyx::Modules::ParseResult{true};
    }
};

// Fix round 1: same containment, but the exception comes from the chosen
// spec's factory instead of Mounts() itself.
struct MountFactoryThrowFake : Onyx::Modules::IGameModule {
    Onyx::Modules::ModuleInfo Info() const override {
        return Onyx::Modules::ModuleInfo{"mountfactorythrow", "MountFactoryThrow", {}, {}};
    }

    Onyx::Modules::ProbeResult Probe(const Onyx::Modules::ProbeInput&) const override {
        return Onyx::Modules::ProbeResult{90, "always"};
    }

    void RegisterTypes(Onyx::Types::TypeRegistrar&) override {}
    void RegisterDecoders(Onyx::Modules::DecoderRegistry&) override {}

    std::vector<Onyx::Modules::MountSpec> Mounts() const override {
        Onyx::Modules::MountSpec spec;
        spec.label = "ThrowingPak";
        spec.extensions = {"pak"};
        spec.mount = [](const std::filesystem::path&)
                -> std::shared_ptr<Onyx::Vfs::IVirtualFileSystem> {
            throw std::runtime_error("mount() boom");
        };
        return {spec};
    }

    Onyx::Modules::ParseResult ParseContainer(Onyx::Modules::ContainerContext& ctx) override {
        Onyx::Domain::AssetEntry e;
        e.name = "flat";
        ctx.roots.push_back(std::move(e));
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

    // Task 8: entries whose bytes come from a mounted inner file
    // (fileIndex != 0) land under outDir/<fileIndex>/ rather than flat in
    // outDir -- this entry's fileIndex is 1 (MountFake pushes it as the
    // first thing appended after the pre-seeded root slot).
    REQUIRE(std::filesystem::exists(outDir / "1" / "inner.bin"));
    std::string content;
    {
        // Scoped so the handle closes before remove_all below -- Windows
        // refuses to delete a directory containing a file still open for
        // read.
        std::ifstream f(outDir / "1" / "inner.bin", std::ios::binary);
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
    // An out-of-range fileIndex never even resolves a target directory --
    // ghost.bin exists in neither outDir nor a per-fileIndex subdirectory.
    CHECK_FALSE(std::filesystem::exists(outDir / "ghost.bin"));
    CHECK_FALSE(std::filesystem::exists(outDir / "99" / "ghost.bin"));

    // The other, valid entry (fileIndex 1) was still extracted despite the
    // bogus one -- a salvage failure on one entry never aborts the extract.
    REQUIRE(std::filesystem::exists(outDir / "1" / "inner.bin"));

    std::filesystem::remove_all(outDir);
    std::filesystem::remove(path);
}

TEST_CASE("Extract: fileIndex 1 succeeds while fileIndex 2's throwing Read is contained "
          "with an error line, not aborted") {
    auto path = write_temp_file("onyx_mounts_test_extract_throw.pak", "ROOT-FILE-CONTENT");
    auto outDir = std::filesystem::temp_directory_path() / "onyx_mounts_extract_throw_out";
    std::filesystem::remove_all(outDir);

    Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(std::make_unique<MountThrowingReadFake>());

    std::ostringstream out;
    int rc = Onyx::Cli::CmdExtract(ws, path, outDir, out);
    CHECK(rc == Onyx::Cli::kOk);   // the throwing entry never escapes as an exception

    // fileIndex 1 ("good") extracts normally with the right bytes, despite
    // fileIndex 2 ("bad") throwing from Read() right after it.
    REQUIRE(std::filesystem::exists(outDir / "1" / "good.bin"));
    CHECK(read_file_string(outDir / "1" / "good.bin") == "GOOD-BYTES");

    // The throwing entry is error-lined and left no file behind.
    CHECK_FALSE(std::filesystem::exists(outDir / "2" / "bad.bin"));
    CHECK(out.str().find("error 'bad.bin'") != std::string::npos);

    std::filesystem::remove_all(outDir);
    std::filesystem::remove(path);
}

TEST_CASE("MountSpec: a throwing Mounts() is contained on Open (sync)") {
    auto path = write_temp_file("onyx_mounts_test_mounts_throw_sync.pak", "root bytes");
    {
        Workspace ws(Onyx::Types::TypeCatalog::Get());
        ws.AddModule(std::make_unique<MountsThrowFake>());

        DocumentId id = 0;
        CHECK_NOTHROW(id = ws.Open(path));   // Mounts() throwing must never escape Open()
        REQUIRE(id != 0);

        auto* doc = ws.Get(id);
        REQUIRE(doc);
        CHECK(doc->ready);
        CHECK(doc->mountedVfs == nullptr);
        REQUIRE(doc->roots.size() == 1);     // parse still proceeded, flat-file
        CHECK(doc->roots[0].name == "flat");

        auto diags = doc->diags.Drain();
        bool sawMountThrew = false;
        for (auto& d : diags) {
            if (d.code == "mountsthrow.mount-threw") {
                sawMountThrew = true;
                CHECK(d.severity == Onyx::Services::Severity::Error);
            }
        }
        CHECK(sawMountThrew);
    }
    std::filesystem::remove(path);
}

TEST_CASE("MountSpec: a throwing Mounts() is contained on OpenAsync") {
    auto path = write_temp_file("onyx_mounts_test_mounts_throw_async.pak", "root bytes");
    {
        Workspace ws(Onyx::Types::TypeCatalog::Get());
        ws.AddModule(std::make_unique<MountsThrowFake>());

        DocumentId readyId = 0;
        bool readyOk = false;
        auto sub = ws.Events().On<TreeReady>([&](auto& e) {
            readyId = e.id;
            readyOk = e.ok;
        });

        DocumentId id = 0;
        CHECK_NOTHROW(id = ws.OpenAsync(path));   // must never escape OpenAsync() either
        REQUIRE(id != 0);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (readyId == 0 && std::chrono::steady_clock::now() < deadline) {
            ws.Jobs().Pump();
            ws.Events().Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        REQUIRE(readyId == id);
        CHECK(readyOk);   // flat-file parse still succeeded

        auto* doc = ws.Get(id);
        REQUIRE(doc);
        CHECK(doc->ready);   // never stranded false: RunParse still reached the end
        CHECK(doc->mountedVfs == nullptr);
        REQUIRE(doc->roots.size() == 1);
        CHECK(doc->roots[0].name == "flat");

        auto diags = doc->diags.Drain();
        bool sawMountThrew = false;
        for (auto& d : diags) {
            if (d.code == "mountsthrow.mount-threw") sawMountThrew = true;
        }
        CHECK(sawMountThrew);
    }
    std::filesystem::remove(path);
}

TEST_CASE("MountSpec: a throwing mount factory is contained on Open (sync)") {
    auto path = write_temp_file("onyx_mounts_test_factory_throw_sync.pak", "root bytes");
    {
        Workspace ws(Onyx::Types::TypeCatalog::Get());
        ws.AddModule(std::make_unique<MountFactoryThrowFake>());

        DocumentId id = 0;
        CHECK_NOTHROW(id = ws.Open(path));
        REQUIRE(id != 0);

        auto* doc = ws.Get(id);
        REQUIRE(doc);
        CHECK(doc->ready);
        CHECK(doc->mountedVfs == nullptr);
        REQUIRE(doc->roots.size() == 1);
        CHECK(doc->roots[0].name == "flat");

        auto diags = doc->diags.Drain();
        bool sawMountThrew = false;
        for (auto& d : diags) {
            if (d.code == "mountfactorythrow.mount-threw") {
                sawMountThrew = true;
                CHECK(d.severity == Onyx::Services::Severity::Error);
            }
        }
        CHECK(sawMountThrew);
    }
    std::filesystem::remove(path);
}

TEST_CASE("MountSpec: a throwing mount factory is contained on OpenAsync") {
    auto path = write_temp_file("onyx_mounts_test_factory_throw_async.pak", "root bytes");
    {
        Workspace ws(Onyx::Types::TypeCatalog::Get());
        ws.AddModule(std::make_unique<MountFactoryThrowFake>());

        DocumentId readyId = 0;
        bool readyOk = false;
        auto sub = ws.Events().On<TreeReady>([&](auto& e) {
            readyId = e.id;
            readyOk = e.ok;
        });

        DocumentId id = 0;
        CHECK_NOTHROW(id = ws.OpenAsync(path));
        REQUIRE(id != 0);

        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (readyId == 0 && std::chrono::steady_clock::now() < deadline) {
            ws.Jobs().Pump();
            ws.Events().Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        REQUIRE(readyId == id);
        CHECK(readyOk);

        auto* doc = ws.Get(id);
        REQUIRE(doc);
        CHECK(doc->ready);   // never stranded false
        CHECK(doc->mountedVfs == nullptr);
        REQUIRE(doc->roots.size() == 1);
        CHECK(doc->roots[0].name == "flat");

        auto diags = doc->diags.Drain();
        bool sawMountThrew = false;
        for (auto& d : diags) {
            if (d.code == "mountfactorythrow.mount-threw") sawMountThrew = true;
        }
        CHECK(sawMountThrew);
    }
    std::filesystem::remove(path);
}

// Task 8: the mounted example, end to end. OnyxBox's real .obxpak mount --
// not a test fake -- proven through Cli::Commands exactly as a real
// consumer would use it: probe picks it up, list --json shows both inner
// containers as subtrees, extract lands each container's bytes correctly,
// a corrupt entry inside one container is salvaged (skipped, not aborted),
// and --strict surfaces the resulting Error diag.
//
// The KEY assertion is anti-root-file-misread: both inner OBX containers
// declare an entry named "shared.txt" but with DIFFERENT bytes. If extract
// ever read through the wrong file (the pak's own bytes, or the other
// inner container's), the byte-for-byte checks below would catch it.
TEST_CASE("OnyxBox obxpak: mounted pak proven end to end through Cli::Commands") {
    auto boxA = BuildObx({{"shared.txt", 2, "FROM-BOX-A"}});
    auto boxB = BuildObx({{"shared.txt", 2, "FROM-BOX-B"},
                           {"corrupt.bin", 0, "xxxx"}},
                          /*corruptLast=*/true);

    auto pakBytes = BuildObxPak({{"boxA.obx", boxA}, {"boxB.obx", boxB}});
    auto path = write_temp_bytes("onyx_mounts_test_obxpak.obxpak", pakBytes);

    auto outDir = std::filesystem::temp_directory_path() / "onyx_mounts_obxpak_out";
    std::filesystem::remove_all(outDir);

    Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(std::make_unique<OnyxBox::OnyxBoxModule>());

    // probe: OBP1 magic scores at the same confidence tier as OBX1 (95),
    // and obx is the winner.
    {
        std::ostringstream out;
        int rc = Onyx::Cli::CmdProbe(ws, path, out);
        CHECK(rc == Onyx::Cli::kOk);
        const std::string text = out.str();
        CHECK(text.find("winner: obx") != std::string::npos);
        CHECK(text.find("95") != std::string::npos);
    }

    // list --json: two subtrees (one per inner OBX), each carrying a
    // "shared.txt" child; the corrupt entry inside boxB shows failed:true.
    {
        std::ostringstream out;
        int rc = Onyx::Cli::CmdList(ws, path, /*json=*/true, out);
        CHECK(rc == Onyx::Cli::kOk);
        const std::string text = out.str();
        CHECK(text.find("\"name\":\"boxA.obx\"") != std::string::npos);
        CHECK(text.find("\"name\":\"boxB.obx\"") != std::string::npos);

        size_t sharedCount = 0;
        for (size_t pos = text.find("\"name\":\"shared.txt\"");
             pos != std::string::npos;
             pos = text.find("\"name\":\"shared.txt\"", pos + 1)) {
            ++sharedCount;
        }
        CHECK(sharedCount == 2);

        CHECK(text.find("\"name\":\"corrupt.bin\"") != std::string::npos);
        CHECK(text.find("\"failed\":true") != std::string::npos);
    }

    // extract: each container's "shared.txt" lands byte-identical to ITS
    // OWN fixture bytes -- never the pak's own bytes, never the sibling
    // container's bytes for the same name. The corrupt entry is skipped
    // (no file written) with its error line in the output.
    {
        std::ostringstream out;
        int rc = Onyx::Cli::CmdExtract(ws, path, outDir, out);
        CHECK(rc == Onyx::Cli::kOk);

        // fileTable[0] = the pak file itself; [1] = boxA.obx (mounted
        // first, in TOC order); [2] = boxB.obx.
        REQUIRE(std::filesystem::exists(outDir / "1" / "shared.txt"));
        REQUIRE(std::filesystem::exists(outDir / "2" / "shared.txt"));
        CHECK(read_file_string(outDir / "1" / "shared.txt") == "FROM-BOX-A");
        CHECK(read_file_string(outDir / "2" / "shared.txt") == "FROM-BOX-B");

        CHECK_FALSE(std::filesystem::exists(outDir / "2" / "corrupt.bin"));
        CHECK(out.str().find("obx.entry.range") != std::string::npos);
    }

    // --strict: this document's parse produced an Error diag (boxB's
    // corrupt entry), so even an unrelated successful decode still exits
    // kStrictErrors.
    {
        std::ostringstream out;
        int rc = Onyx::Cli::CmdDecode(ws, path, "shared.txt", /*strict=*/true, out);
        CHECK(rc == Onyx::Cli::kStrictErrors);
    }

    std::filesystem::remove_all(outDir);
    std::filesystem::remove(path);
}

// F6: a mount that succeeds but whose walk names nothing -- an empty OBP1
// header (count 0) here -- must still surface a diag. Silently returning
// zero roots would look identical to "this pak really has nothing", which
// is exactly the confusing case this Warning exists to rule out.
TEST_CASE("OnyxBox obxpak: an empty mounted pak reports exactly one Warning diag and zero roots") {
    auto pakBytes = BuildObxPak({});  // magic + count(0), no inner files at all
    auto path = write_temp_bytes("onyx_mounts_test_obxpak_empty.obxpak", pakBytes);

    Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(std::make_unique<OnyxBox::OnyxBoxModule>());

    DocumentId id = ws.Open(path);
    REQUIRE(id != 0);
    Document* doc = ws.Get(id);
    REQUIRE(doc);
    CHECK(doc->roots.empty());

    auto diags = doc->diags.Drain();
    int warnings = 0;
    bool sawEmptyDiag = false;
    for (auto& d : diags) {
        if (d.severity == Onyx::Services::Severity::Warning) ++warnings;
        if (d.code == "onyxbox.pak-empty") sawEmptyDiag = true;
    }
    CHECK(warnings == 1);
    CHECK(sawEmptyDiag);

    ws.Close(id);
    std::filesystem::remove(path);
}

// Task 8 fix round 1: the (fileIndex, name) re-keying in FindTocEntry/
// DecodeImage/DecodeText is correct by inspection but was unproven by the
// e2e test above -- CmdDecode's name-only DFS (Commands.cpp's
// FindEntryByName) always resolves to boxA's "shared.txt" first, which the
// OLD name-only FindTocEntry would also have gotten right by coincidence.
// This test bypasses CmdDecode entirely: it walks doc->roots by hand to
// reach boxB's "shared.txt" AssetEntry specifically, then invokes the
// registered text decoder directly (the same DecoderRegistry path the GUI
// uses) against that exact entry. Under the pre-fix name-only lookup, this
// would have decoded boxA's bytes ("FROM-BOX-A") for a request that named
// boxB's entry -- this assertion is the one that distinguishes the fix
// from the bug.
TEST_CASE("OnyxBox obxpak: decoding boxB's shared.txt by entry (not by name-only lookup) returns boxB's bytes") {
    auto boxA = BuildObx({{"shared.txt", 2, "FROM-BOX-A"}});
    auto boxB = BuildObx({{"shared.txt", 2, "FROM-BOX-B"},
                           {"corrupt.bin", 0, "xxxx"}},
                          /*corruptLast=*/true);

    auto pakBytes = BuildObxPak({{"boxA.obx", boxA}, {"boxB.obx", boxB}});
    auto path = write_temp_bytes("onyx_mounts_test_obxpak_decode_disambig.obxpak", pakBytes);

    Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(std::make_unique<OnyxBox::OnyxBoxModule>());

    DocumentId id = ws.Open(path);
    REQUIRE(id != 0);
    Document* doc = ws.Get(id);
    REQUIRE(doc);
    REQUIRE(doc->roots.size() == 2);

    // Find boxB's subtree by name, then its "shared.txt" child directly --
    // no name-only tree search across both subtrees.
    const Onyx::Domain::AssetEntry* boxBSubtree = nullptr;
    for (const auto& root : doc->roots) {
        if (root.name == "boxB.obx") { boxBSubtree = &root; break; }
    }
    REQUIRE(boxBSubtree != nullptr);

    const Onyx::Domain::AssetEntry* boxBShared = nullptr;
    for (const auto& child : boxBSubtree->children) {
        if (child.name == "shared.txt") { boxBShared = &child; break; }
    }
    REQUIRE(boxBShared != nullptr);
    // Sanity: this entry really is addressed at boxB's file-table slot,
    // not boxA's.
    CHECK(boxBShared->source.fileIndex == 2);

    // Invoke the text decoder the way the GUI path does: through the
    // module's registered DecoderRegistry entry for this exact AssetEntry.
    Onyx::Services::Progress progress;
    Onyx::Modules::DecodeContext ctx{*doc, *boxBShared, doc->diags, progress};
    std::optional<Onyx::Modules::DecodedText> decoded = ws.Decoders().DecodeText(ctx);

    REQUIRE(decoded.has_value());
    CHECK(decoded->text == "FROM-BOX-B");   // NOT "FROM-BOX-A" -- the bug this fix closes

    ws.Close(id);
    std::filesystem::remove(path);
}
