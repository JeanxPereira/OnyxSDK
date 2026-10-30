#pragma once

#include <Onyx/Core/Domain/MediaKind.h>
#include <Onyx/Core/Result.h>

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace Onyx::Cli {

// `payload` points at the decoded object of the exporter's kind: Domain::Scene for Mesh,
// Domain::Texture for Image, Domain::Audio for Audio.
using ExportFn = std::function<Result<void>(const void* payload, const std::filesystem::path& out)>;

struct Exporter {
    std::string format;
    Domain::MediaKind kind = Domain::MediaKind::Unknown;
    ExportFn write;
};

class Exporters {
public:
    // Registering a format again replaces the previous exporter.
    void Add(std::string format, Domain::MediaKind kind, ExportFn write);
    const Exporter* Find(std::string_view format) const;

private:
    std::map<std::string, Exporter, std::less<>> m_byFormat;
};

}
