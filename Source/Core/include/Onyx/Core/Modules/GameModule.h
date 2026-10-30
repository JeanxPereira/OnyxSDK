#pragma once

#include <Onyx/Core/Services/SettingsSchema.h>
#include <Onyx/Core/Types/TypeRegistrar.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Onyx::Vfs {
class IVirtualFileSystem;
}

namespace Onyx::Modules {

class DecoderRegistry;
struct ContainerContext;

struct OpenFilter {
    std::string label;
    std::vector<std::string> extensions;
};

struct ModuleInfo {
    std::string id;
    std::string displayName;
    std::vector<std::string> hints;
    std::vector<OpenFilter> openFilters;
};

struct ProbeInput {
    std::filesystem::path path;
    std::span<const std::byte> header;
    uint64_t fileSize = 0;
};

struct ProbeResult {
    int confidence = 0;
    std::string reason;
};

struct MountSpec {
    std::string label;
    std::vector<std::string> extensions;
    std::function<std::shared_ptr<Vfs::IVirtualFileSystem>(const std::filesystem::path&)> mount;
};

struct ParseResult {
    bool ok = false;
};

class IGameModule {
public:
    virtual ~IGameModule() = default;
    virtual ModuleInfo Info() const = 0;
    virtual ProbeResult Probe(const ProbeInput&) const = 0;
    virtual void RegisterTypes(Types::TypeRegistrar&) = 0;
    virtual void RegisterDecoders(DecoderRegistry&) = 0;
    virtual void RegisterSettings(Services::SettingsSchema&) {}
    // The first-declared spec whose extensions match the opened path wins.
    virtual std::vector<MountSpec> Mounts() const { return {}; }
    virtual ParseResult ParseContainer(ContainerContext&) = 0;
};

}
