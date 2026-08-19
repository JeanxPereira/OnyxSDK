// onyxbox-cli -- the generic headless CLI (Onyx::Cli::Run) wired against
// the OnyxBox example module. Proves Task 6's probe/list/extract/decode
// commands end to end without depending on any real game format.
//
// Task 14 (M4) adds two more subcommands, handled here rather than inside
// Onyx::Cli::Run() (Source/Cli/Commands.cpp, which compiles into the
// Vulkan-free Onyx_Core -- see Include/Onyx/Cli/Render.h's top comment for
// why `render` cannot live there):
//   render <container> <entry> --out out.png [--width N] [--height N]
//       Decodes `entry` through the Scene capability and rasterizes it
//       headlessly via Onyx::Cli::CmdRender (Source/Cli/Render.cpp, linked
//       into this executable's own target, which is the one place
//       Onyx_Core and Onyx::Rendering may meet without a link cycle).
//   write-render-fixture <path>
//       Writes a canonical OBX1 file holding one kind=3 (mesh) entry named
//       "cube" -- the fixture Examples/OnyxCli/RenderTest.cmake's
//       OnyxCliRender ctest feeds to `render`. Kept as a CLI subcommand
//       (Core-only, no Vulkan) rather than a checked-in binary fixture so
//       the exact bytes are visible in this file and never go stale
//       relative to OnyxBoxModule's TOC format.

#include <Onyx/Cli/Commands.h>
#include <Onyx/Cli/Render.h>
#include <Onyx/Modules/Workspace.h>
#include <Onyx/Types/TypeCatalog.h>

#include <OnyxBoxModule.h>

#include <cstdint>
#include <cstdlib>
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

// One kind=3 (mesh) entry named "cube": an orange-ish opaque box centered
// at the origin, half-extents (1,1,1) -- see Examples/OnyxBox/
// OnyxBoxModule.h's top comment for the 12-float payload layout.
int WriteRenderFixture(const std::string& path) {
    std::vector<uint8_t> payload;
    PutF32LE(payload, 0.85f); PutF32LE(payload, 0.35f);  // baseColor.rg
    PutF32LE(payload, 0.20f); PutF32LE(payload, 1.00f);  // baseColor.ba
    PutF32LE(payload, 0.0f);  PutF32LE(payload, 0.0f);   PutF32LE(payload, 0.0f); // position xyz
    PutF32LE(payload, 1.0f);  PutF32LE(payload, 1.0f);   PutF32LE(payload, 1.0f); // half-extents xyz
    PutF32LE(payload, 0.0f);  PutF32LE(payload, 0.0f);   // padding

    const std::string name = "cube";
    const uint32_t headerSize = 4 + 4 + uint32_t(2 + name.size() + 1 + 4 + 4);
    const uint32_t payloadOffset = headerSize;
    const uint32_t payloadSize = uint32_t(payload.size());

    std::vector<uint8_t> buf;
    buf.insert(buf.end(), {uint8_t('O'), uint8_t('B'), uint8_t('X'), uint8_t('1')});
    PutU32LE(buf, 1);
    PutTocHeader(buf, name, /*kind=*/3, payloadOffset, payloadSize);
    buf.insert(buf.end(), payload.begin(), payload.end());

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        std::cerr << "write-render-fixture: cannot open " << path << " for writing\n";
        return Onyx::Cli::kUsage;
    }
    f.write(reinterpret_cast<const char*>(buf.data()), std::streamsize(buf.size()));
    if (!f) {
        std::cerr << "write-render-fixture: write failed for " << path << "\n";
        return Onyx::Cli::kUsage;
    }
    std::cout << "wrote " << path << " (" << buf.size() << " bytes, 1 entry: cube)\n";
    return Onyx::Cli::kOk;
}

// Parses `render <container> <entry> --out out.png [--width N] [--height
// N] [--game <hint>]` and dispatches to Onyx::Cli::CmdRender. Kept apart
// from Onyx::Cli::Run()'s own argv parsing (Source/Cli/Commands.cpp) since
// Run() must stay Vulkan-free -- see this file's top comment.
int RunRender(Onyx::Modules::Workspace& ws, int argc, char** argv) {
    std::vector<std::string> positional;
    std::string outPath, gameHint;
    int width = 512, height = 512;

    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) {
            outPath = argv[++i];
        } else if (a == "--width" && i + 1 < argc) {
            width = std::atoi(argv[++i]);
        } else if (a == "--height" && i + 1 < argc) {
            height = std::atoi(argv[++i]);
        } else if (a == "--game" && i + 1 < argc) {
            gameHint = argv[++i];
        } else {
            positional.push_back(a);
        }
    }

    if (positional.size() < 2 || outPath.empty()) {
        std::cerr << "usage: render <container> <entry> --out out.png [--width N] [--height N] "
                     "[--game <hint>]\n"
                     "  Entry resolution: first name match, pre-order depth-first search over\n"
                     "  the opened document's tree (same rule `decode` uses).\n"
                     "  Exit 77 (not a usage error) when no Vulkan-capable device/driver is found.\n";
        return Onyx::Cli::kUsage;
    }

    return Onyx::Cli::CmdRender(ws, positional[0], positional[1], outPath, width, height, std::cout,
                                gameHint);
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

    Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
    ws.AddModule(std::make_unique<OnyxBox::OnyxBoxModule>());

    if (argc >= 2 && std::strcmp(argv[1], "render") == 0) {
        return RunRender(ws, argc, argv);
    }

    return Onyx::Cli::Run(ws, argc, argv, std::cout, std::cerr);
}
