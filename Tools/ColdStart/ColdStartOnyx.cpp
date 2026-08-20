// Cold-start exam for <Onyx/Onyx.h> -- the "exam" T2's public-surface audit
// (docs/design/2026-08-19-public-surface-audit.md) proved by hand, promoted
// into a checked-in TU that CI runs on every push.
//
// The claim under test is README's "Consuming Onyx", read verbatim: a
// stranger who clones the repo, links `Onyx::Onyx` and writes
// `#include <Onyx/Onyx.h>` can build a working toolkit. That claim has two
// halves and this file is exercised against BOTH:
//
//   1. COMPILE, headers only. `cl /c` against -I Include plus the
//      third-party dirs the umbrella needs -- no -I Source, no
//      -I build/generated (Include/Onyx/Version.h is checked in since
//      fbd54d3, so the umbrella's first include resolves without ever
//      running CMake). See .github/workflows/ci.yml's "Cold-start header
//      exam" step, which names its include dirs explicitly, one at a time,
//      exactly as T2/T8 proved this by hand with a throwaway TU outside the
//      repo. Keeping the TU checked in (rather than authored fresh by CI
//      every run) is what keeps this an exam that can be rerun locally, not
//      a step that only ever existed inside a workflow log.
//
//   2. LINK AND RUN, against exactly what README tells a consumer to link.
//      The `ColdStartToolkit` target (Tools/ColdStart/CMakeLists.txt) links
//      `Onyx::Onyx` and NOTHING else, and the `ColdStart` ctest entry runs
//      the result. This half is new at the v1.0.0 final review: through
//      v0.6.0 the exam was compile-only, and a compile-only exam is
//      structurally blind to the defect the umbrella most needs guarding
//      against -- a declaration whose symbol ships in a target the consumer
//      did not link. That defect (the audit's blocking gap G1, seen from
//      the umbrella's side) survived four review rounds precisely because
//      every round asked "does it compile" and none asked "does it link".
//      `Onyx::Onyx` links Onyx_Core/Onyx_Render/Onyx_Shell; if
//      <Onyx/Onyx.h> ever again names a header from Onyx_Exchange,
//      Onyx_CliRender, Onyx_TestKit or Onyx_Media, this TU stops linking
//      the moment it calls one -- which is why the body below CALLS things
//      rather than merely naming them.
//
// The linking half does not weaken the compile half: the compile step still
// runs on the raw invocation with no CMake-supplied include dirs at all.
// The CMake target adds only what a real consumer's own `Onyx::Onyx` link
// adds (PUBLIC include dirs and compile definitions); `Source/` is PRIVATE
// on every Onyx target and never propagates.
//
// ── Why this file does real work ──────────────────────────────────────────
// Through v0.6.0 this TU was `#include <Onyx/Onyx.h>` plus `int main(){}` --
// the audit's own "Variant A", which the audit itself caught FALSE-PASSING:
// it compiled while the umbrella was missing eight headers a real toolkit
// needs, because a TU that names nothing cannot discover an unreachable
// name. Variant B (the same toolkit doing real work, umbrella only) is what
// found the 6 errors behind gap G2. The body below is Variant B's shape:
// one IGameModule with every method actually implemented (RegisterDecoders
// gets a BODY -- the seam Variant A ducked, and it needs DecoderRegistry to
// be a complete type), a synthetic container written, opened and parsed
// through a Workspace, a decoder invoked, a selection resolved through a
// NodePath, plus a theme, a resolved resource path and a logged line.
// Everything a stranger's first hour would touch.
//
// The GUI/GPU boot path is compiled and LINKED but gated behind `--boot` so
// the ctest entry stays headless: the linker resolves a symbol whether or
// not the branch is taken at runtime, so gating costs the exam nothing.

#include <Onyx/Onyx.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace ColdStart {

// ── A synthetic container, invented here so this exam depends on no game
// format and on no Examples/ target: magic "CSX1", u32 count, then per
// entry u16 nameLen | name | u32 payloadOffset | u32 payloadSize. Every
// payload is UTF-8 text. ─────────────────────────────────────────────────
constexpr char kMagic[4] = {'C', 'S', 'X', '1'};

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

bool ReadU16LE(Onyx::Vfs::IFile& f, uint16_t& out) {
    uint8_t b[2];
    if (f.Read(b, sizeof(b)) != sizeof(b)) return false;
    out = uint16_t(uint16_t(b[0]) | uint16_t(uint16_t(b[1]) << 8));
    return true;
}

bool ReadU32LE(Onyx::Vfs::IFile& f, uint32_t& out) {
    uint8_t b[4];
    if (f.Read(b, sizeof(b)) != sizeof(b)) return false;
    out = uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) |
          (uint32_t(b[3]) << 24);
    return true;
}

std::filesystem::path WriteFixture() {
    const std::string first  = "hello cold start";
    const std::string second = "a second entry";

    // header = magic + count + two TOC records ("one"/"two", 3 chars each)
    const uint32_t headerSize   = 4 + 4 + 2 * (2 + 3 + 4 + 4);
    const uint32_t firstOffset  = headerSize;
    const uint32_t secondOffset = firstOffset + uint32_t(first.size());

    std::vector<uint8_t> buf;
    buf.insert(buf.end(), kMagic, kMagic + 4);
    PutU32LE(buf, 2);

    PutU16LE(buf, 3);
    buf.insert(buf.end(), {'o', 'n', 'e'});
    PutU32LE(buf, firstOffset);
    PutU32LE(buf, uint32_t(first.size()));

    PutU16LE(buf, 3);
    buf.insert(buf.end(), {'t', 'w', 'o'});
    PutU32LE(buf, secondOffset);
    PutU32LE(buf, uint32_t(second.size()));

    buf.insert(buf.end(), first.begin(), first.end());
    buf.insert(buf.end(), second.begin(), second.end());

    auto path = std::filesystem::temp_directory_path() / "onyx_coldstart.csx";
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
    out.close();
    return path;
}

// ── The toolkit's one game module. Every override has a real body; nothing
// here is a stub, because a stub is exactly how Variant A passed while the
// surface was broken. ────────────────────────────────────────────────────
class ColdStartModule final : public Onyx::Modules::IGameModule {
public:
    Onyx::Modules::ModuleInfo Info() const override {
        Onyx::Modules::ModuleInfo info;
        info.id          = "csx";
        info.displayName = "Cold Start";
        info.hints       = {"csx", "coldstart"};
        info.openFilters = {{"Cold Start containers", {"csx"}}};
        return info;
    }

    Onyx::Modules::ProbeResult Probe(const Onyx::Modules::ProbeInput& in) const override {
        if (in.header.size() >= 4 && std::memcmp(in.header.data(), kMagic, 4) == 0) {
            return {95, "CSX1 magic at 0"};
        }
        return {0, "no CSX1 magic"};
    }

    void RegisterTypes(Onyx::Types::TypeRegistrar& reg) override {
        Onyx::Types::TypeInfo spec;
        spec.key   = "note";
        spec.label = "Note";
        spec.media = Onyx::Domain::MediaKind::Unknown;   // text has no MediaKind of its own
        m_note     = reg.Add(spec);
    }

    // The seam Variant A ducked: declaring this without a body compiles
    // even when DecoderRegistry is an incomplete type. Registering a real
    // decoder does not.
    void RegisterDecoders(Onyx::Modules::DecoderRegistry& reg) override {
        reg.Text(m_note, [](Onyx::Modules::DecodeContext& ctx)
                             -> std::optional<Onyx::Modules::DecodedText> {
            if (!ctx.doc.file || ctx.entry.source.Empty()) return std::nullopt;
            if (!ctx.doc.file->Seek(int64_t(ctx.entry.source.offset), SEEK_SET))
                return std::nullopt;
            std::string text(size_t(ctx.entry.source.size), '\0');
            if (ctx.doc.file->Read(text.data(), text.size()) != text.size())
                return std::nullopt;
            return Onyx::Modules::DecodedText{std::move(text), ""};
        });
    }

    Onyx::Modules::ParseResult ParseContainer(Onyx::Modules::ContainerContext& ctx) override {
        // ContainerContext::path: the field a consumer (GoWToolkit) found
        // missing -- a module has no other way to learn which file it is
        // parsing, needed both to fill Domain::AssetEntry::wadName and for
        // path-relative work at parse time (e.g. checking for a sibling
        // config file next to the container). Captured here so main() can
        // confirm it names the exact fixture path Workspace::Open was
        // handed, not just that the field compiles.
        m_lastParsedPath = ctx.path;

        char magic[4] = {};
        if (ctx.file.Read(magic, sizeof(magic)) != sizeof(magic) ||
            std::memcmp(magic, kMagic, 4) != 0) {
            return {false};
        }
        uint32_t count = 0;
        if (!ReadU32LE(ctx.file, count)) return {false};

        Onyx::Domain::AssetEntry root;
        root.name        = "csx";
        root.displayName = "Cold Start container";

        for (uint32_t i = 0; i < count; ++i) {
            uint16_t nameLen = 0;
            if (!ReadU16LE(ctx.file, nameLen)) break;
            std::string name(nameLen, '\0');
            if (ctx.file.Read(name.data(), name.size()) != name.size()) break;
            uint32_t offset = 0, size = 0;
            if (!ReadU32LE(ctx.file, offset) || !ReadU32LE(ctx.file, size)) break;

            Onyx::Domain::AssetEntry child;
            child.name          = std::move(name);
            child.typeId        = m_note;
            child.kind          = Onyx::Domain::MediaKind::Unknown;
            child.source.offset = offset;
            child.source.size   = size;
            if (offset + uint64_t(size) > ctx.file.Size()) {
                child.flags = Onyx::Domain::NodeFlags::Failed;
                ctx.diags.Report({Onyx::Services::Severity::Warning, "csx.entry.range",
                                  "payload runs past end of file"});
            }
            root.children.push_back(std::move(child));
        }

        const bool any = !root.children.empty();
        ctx.roots.push_back(std::move(root));
        return {any};
    }

    std::filesystem::path m_lastParsedPath;   // what ContainerContext::path handed ParseContainer

private:
    Onyx::Types::TypeId m_note{};
};

// ── The GUI boot path. Compiled and linked always, executed only under
// `--boot`: this exam runs headless in ctest, but the linker still has to
// resolve every symbol below, which is the point. ────────────────────────
int Boot() {
    Onyx::Threading::MarkMainThread();
    Onyx::App::Window::initNative();
    Onyx::App::Window window;
    window.app().SetRegistrar([](Onyx::App::App& app) {
        app.AddModule(std::make_unique<ColdStartModule>());
    });
    window.run();
    return 0;
}

} // namespace ColdStart

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--boot") == 0) {
        return ColdStart::Boot();
    }

    ONYX_LOG_INFO("coldstart", "umbrella-only toolkit starting");

    // 1. A Workspace, a module, a real container parsed end to end.
    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    auto                        ownedModule = std::make_unique<ColdStart::ColdStartModule>();
    ColdStart::ColdStartModule* modulePtr   = ownedModule.get();
    ws.AddModule(std::move(ownedModule));

    const auto                   path = ColdStart::WriteFixture();
    const Onyx::Modules::DocumentId id = ws.Open(path);
    if (id == 0) {
        std::fprintf(stderr, "cold-start: Workspace::Open refused the fixture\n");
        return 1;
    }
    Onyx::Modules::Document* doc = ws.Get(id);
    if (!doc || doc->roots.size() != 1 || doc->roots[0].children.size() != 2) {
        std::fprintf(stderr, "cold-start: parsed tree is not 1 root / 2 children\n");
        return 1;
    }
    // ContainerContext::path must have named exactly the fixture Workspace
    // was told to open -- the field this milestone added specifically so a
    // module can answer "which file am I parsing" (see ParseContainer above).
    if (modulePtr->m_lastParsedPath != path) {
        std::fprintf(stderr, "cold-start: ContainerContext::path ('%s') did not match "
                              "the opened fixture ('%s')\n",
                     modulePtr->m_lastParsedPath.string().c_str(), path.string().c_str());
        return 1;
    }

    // 2. Probe ranking -- the module's own Probe() reached through the
    //    Workspace, not called directly.
    const auto ranking = ws.Probe(path);
    if (ranking.rows.empty() || ranking.winner == nullptr) {
        std::fprintf(stderr, "cold-start: nothing claimed the fixture\n");
        return 1;
    }

    // 3. Id-safe selection: address a node by NodePath and resolve it.
    Onyx::Modules::NodePath sel;
    sel.indices                            = {0, 1};
    const Onyx::Domain::AssetEntry* picked = Onyx::Modules::Resolve(*doc, sel);
    if (!picked || picked->name != "two") {
        std::fprintf(stderr, "cold-start: NodePath{0,1} did not resolve to \"two\"\n");
        return 1;
    }

    // 4. Decode it through the registry the module populated.
    Onyx::Services::DiagSink     diags;
    Onyx::Services::Progress     progress;
    Onyx::Modules::DecodeContext dctx{*doc, *picked, diags, progress};
    const auto                   text = ws.Decoders().DecodeText(dctx);
    if (!text || text->text != "a second entry") {
        std::fprintf(stderr, "cold-start: text decode did not round-trip\n");
        return 1;
    }

    // 5. Services a first-hour toolkit reaches for: resource paths and the
    //    renderer's own camera math.
    // ::PathUtils, not Onyx::Services::PathUtils -- the audit's G5 half that
    // carries past v1.0 (CHANGELOG "Known gaps"). Spelled the way a
    // consumer actually has to spell it today, on purpose.
    const std::string resolved = ::PathUtils::resolvePath("third_party/fonts");
    Onyx::Rendering::Camera camera;
    const glm::mat4 viewProj = camera.GetProjectionMatrix(16.0f / 9.0f) * camera.GetViewMatrix();

    ws.Close(id);
    std::error_code ec;
    std::filesystem::remove(path, ec);

    ONYX_LOG_INFO("coldstart", "ok: decoded {} bytes, fonts resolve under {}",
                  text->text.size(), resolved);

    // Keep the camera math observable so nothing above can be folded away;
    // the exam is about symbols resolving, not about the number.
    return (viewProj[3][3] == 0.0f && resolved.empty()) ? 1 : 0;
}
