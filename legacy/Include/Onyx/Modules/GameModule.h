#pragma once
#include <Onyx/Types/TypeRegistrar.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Onyx::Vfs { class IVirtualFileSystem; }
namespace Onyx::Modules {

struct OpenFilter {
    std::string label;                    // "God of War Ragnarok"
    std::vector<std::string> extensions;  // lowercase, no dot: {"wad"}
};

struct ModuleInfo {
    std::string id;            // "gowr" — namespace for types/settings/diags
    std::string displayName;   // "God of War Ragnarok"
    std::vector<std::string> hints;       // CLI: --game gowr|ragnarok
    std::vector<OpenFilter>  openFilters;
};

struct ProbeInput {
    std::filesystem::path path;
    std::span<const std::byte> header;   // first 64 KiB, pre-read by the host
    uint64_t fileSize = 0;
};

struct ProbeResult {
    int         confidence = 0;   // 0..100
    std::string reason;           // "LZ4 frame magic at 0"
};

struct MountSpec {
    std::string label;
    std::vector<std::string> extensions;  // lowercase, no dot
    // Factory returns nullptr when the file cannot be mounted after all.
    std::function<std::shared_ptr<Vfs::IVirtualFileSystem>(
        const std::filesystem::path&)> mount;
};

class DecoderRegistry;    // Task 3
struct ContainerContext;  // Task 2

struct ParseResult {
    bool ok = false;      // false = nothing usable (tree may still be partial)
};

class IGameModule {
public:
    virtual ~IGameModule() = default;
    virtual ModuleInfo  Info() const = 0;
    virtual ProbeResult Probe(const ProbeInput&) const = 0;
    virtual void        RegisterTypes(Types::TypeRegistrar&) = 0;
    virtual void        RegisterDecoders(DecoderRegistry&) = 0;
    // When more than one returned MountSpec's extensions matches the
    // opened path, the first-declared spec (lowest index in the returned
    // vector) wins -- later matches are never tried.
    virtual std::vector<MountSpec> Mounts() const { return {}; }
    virtual ParseResult ParseContainer(ContainerContext&) = 0;
};

} // namespace Onyx::Modules
