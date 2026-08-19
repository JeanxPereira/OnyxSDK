// onyxbox-cli -- the generic headless CLI (Onyx::Cli::Run) wired against
// the OnyxBox example module. Proves Task 6's probe/list/extract/decode
// commands end to end without depending on any real game format.
//
// `render` (M4 Task 14) used to be handled here, apart from
// Onyx::Cli::Run()'s own argv parsing, because Run() compiles into
// Onyx_Core, which must stay Vulkan-free. M5 Task 6 moved that argv
// parsing INTO Run() itself (Source/Cli/Commands.cpp) -- Run() reaches the
// actual renderer through an injected `RenderFn` hook (Include/Onyx/Cli/
// Commands.h's own doc comment explains why this stays a hook rather than
// a direct call) instead of implementing it locally. This file's only job
// for `render` now is supplying that hook: `Onyx::Cli::CmdRender` itself
// (Include/Onyx/Cli/Render.h), passed straight through with no wrapper --
// CmdRender's own parameter list already matches RenderFn's, since
// Onyx_CliRender (root CMakeLists.txt) ships CmdRender as a real linkable
// symbol now (the public-surface-audit's G1 fix; see Render.h's top
// comment for the full history). A second toolkit that links
// Onyx::CliRender and calls `Onyx::Cli::Run(ws, argc, argv, ..., exportFn,
// Onyx::Cli::CmdRender)` gets `render` -- flags, canonical views, --strict,
// entry resolution, everything -- with no local parsing of its own,
// exactly like this file now has none.
//
// write-render-fixture / write-render-strict-fixture <path>
//     Writes a canonical OBX1 fixture (Core-only, no Vulkan) rather than a
//     checked-in binary, so the exact bytes stay visible in this file and
//     never go stale relative to OnyxBoxModule's TOC format.
//       write-render-fixture: one kind=3 (mesh) entry named "cube" -- the
//         fixture Examples/OnyxCli/RenderTest.cmake and RenderViewsTest.cmake
//         feed to `render`.
//       write-render-strict-fixture: the same "cube" entry PLUS a "bad"
//         entry whose declared payload range is out of bounds -- OnyxBox-
//         Module's own TOC walk flags that Failed and reports an
//         "obx.entry.range" Error diag for the whole document (same shape
//         Tests/cli_test.cpp's WriteSampleBox uses for `decode --strict`'s
//         own test), regardless of which entry `render` is later asked to
//         decode. RenderStrictTest.cmake feeds this to `render --strict`.
//
// M5 Task 5 adds `decode ... --to gltf --out <path>` through Onyx::Cli::
// Run() the same way (Commands.cpp's own CmdDecode already parses
// `--to`/`--out`, see Commands.h), with an `exportFn` hook: Onyx::Cli::
// MakeGltfExportFn (Include/Onyx/Cli/Gltf.h, implemented in
// Examples/OnyxCli/Gltf.cpp -- see Gltf.h's own top comment for why THAT
// hook still needs a wrapper where CmdRender does not: Gltf.cpp is this
// executable's only home for Onyx::Exchange::ExportSceneData, never a
// shipped SDK library of its own). Passed on every invocation, not just
// ones that use --to: CmdDecode only ever calls the hook when --to was
// actually given, so there is nothing to gate here.

#include <Onyx/Cli/Commands.h>
#include <Onyx/Cli/Gltf.h>
#include <Onyx/Cli/Render.h>
#include <Onyx/Modules/Workspace.h>
#include <Onyx/Types/TypeCatalog.h>

#include <OnyxBoxModule.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

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

// The kind=3 (mesh) "cube" payload both fixtures below share: an
// orange-ish opaque box centered at the origin, half-extents (1,1,1) --
// see Examples/OnyxBox/OnyxBoxModule.h's top comment for the 12-float
// payload layout.
std::vector<uint8_t> CubeMeshPayload() {
    std::vector<uint8_t> payload;
    PutF32LE(payload, 0.85f); PutF32LE(payload, 0.35f);  // baseColor.rg
    PutF32LE(payload, 0.20f); PutF32LE(payload, 1.00f);  // baseColor.ba
    PutF32LE(payload, 0.0f);  PutF32LE(payload, 0.0f);   PutF32LE(payload, 0.0f); // position xyz
    PutF32LE(payload, 1.0f);  PutF32LE(payload, 1.0f);   PutF32LE(payload, 1.0f); // half-extents xyz
    PutF32LE(payload, 0.0f);  PutF32LE(payload, 0.0f);   // padding
    return payload;
}

bool WriteObxFile(const std::string& path, const std::vector<uint8_t>& buf, const char* subcommand) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        std::cerr << subcommand << ": cannot open " << path << " for writing\n";
        return false;
    }
    f.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
    if (!f) {
        std::cerr << subcommand << ": write failed for " << path << "\n";
        return false;
    }
    return true;
}

int WriteRenderFixture(const std::string& path) {
    const std::vector<uint8_t> payload = CubeMeshPayload();
    const std::string name = "cube";
    const uint32_t headerSize = 4 + 4 + uint32_t(2 + name.size() + 1 + 4 + 4);
    const uint32_t payloadOffset = headerSize;
    const uint32_t payloadSize = uint32_t(payload.size());

    std::vector<uint8_t> buf;
    buf.insert(buf.end(), {uint8_t('O'), uint8_t('B'), uint8_t('X'), uint8_t('1')});
    PutU32LE(buf, 1);
    PutTocHeader(buf, name, /*kind=*/3, payloadOffset, payloadSize);
    buf.insert(buf.end(), payload.begin(), payload.end());

    if (!WriteObxFile(path, buf, "write-render-fixture")) return Onyx::Cli::kUsage;
    std::cout << "wrote " << path << " (" << buf.size() << " bytes, 1 entry: cube)\n";
    return Onyx::Cli::kOk;
}

// See this file's top comment for why this fixture exists: a good "cube"
// entry (kind=3) that `render` decodes fine, PLUS a "bad" entry whose
// declared payload range is deliberately beyond EOF, so OnyxBoxModule's
// TOC walk flags it Failed and reports an "obx.entry.range" Error diag
// for the WHOLE document -- present in doc->diags regardless of which
// entry is later rendered, exactly what `render --strict` needs to prove
// its exit code changes without the render itself failing.
int WriteRenderStrictFixture(const std::string& path) {
    const std::vector<uint8_t> cubePayload = CubeMeshPayload();
    const std::string cubeName = "cube";
    const std::string badName = "bad";

    const uint32_t headerSize =
        4 + 4 + uint32_t(2 + cubeName.size() + 1 + 4 + 4) + uint32_t(2 + badName.size() + 1 + 4 + 4);
    const uint32_t cubeOffset = headerSize;
    const uint32_t cubeSize = uint32_t(cubePayload.size());
    const uint32_t fileEnd = cubeOffset + cubeSize;
    const uint32_t badOffset = fileEnd + 1000; // deliberately beyond EOF -- see this function's own comment
    const uint32_t badSize = 4;

    std::vector<uint8_t> buf;
    buf.insert(buf.end(), {uint8_t('O'), uint8_t('B'), uint8_t('X'), uint8_t('1')});
    PutU32LE(buf, 2);
    PutTocHeader(buf, cubeName, /*kind=*/3, cubeOffset, cubeSize);
    PutTocHeader(buf, badName, /*kind=*/0, badOffset, badSize);
    buf.insert(buf.end(), cubePayload.begin(), cubePayload.end());

    if (!WriteObxFile(path, buf, "write-render-strict-fixture")) return Onyx::Cli::kUsage;
    std::cout << "wrote " << path << " (" << buf.size() << " bytes, 2 entries: cube, bad)\n";
    return Onyx::Cli::kOk;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "write-render-fixture") == 0) {
        if (argc < 3) {
            std::cerr << "usage: write-render-fixture <path>\n";
            return Onyx::Cli::kUsage;
        }
        return WriteRenderFixture(argv[2]);
    }
    if (argc >= 2 && std::strcmp(argv[1], "write-render-strict-fixture") == 0) {
        if (argc < 3) {
            std::cerr << "usage: write-render-strict-fixture <path>\n";
            return Onyx::Cli::kUsage;
        }
        return WriteRenderStrictFixture(argv[2]);
    }

    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(std::make_unique<OnyxBox::OnyxBoxModule>());

    return Onyx::Cli::Run(ws, argc, argv, std::cout, std::cerr,
                          Onyx::Cli::MakeGltfExportFn(/*embedBuffers=*/true, /*includeSkin=*/true),
                          Onyx::Cli::CmdRender);
}
