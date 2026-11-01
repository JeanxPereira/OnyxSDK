#include <Onyx/Exchange/Exporters.h>

#include <Onyx/Core/Services/Paths.h>
#include <Onyx/Exchange/Gltf.h>
#include <Onyx/Exchange/Png.h>
#include <Onyx/Exchange/Wav.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace Onyx::Exchange {

void AddExporters(Cli::VerbTable& table) {
    Cli::Exporters& exporters = table.Exporters();
    exporters.Add("gltf", Domain::MediaKind::Mesh, [](const void* payload, const std::filesystem::path& out) {
        std::string extension = Services::Paths::ToUtf8(out.extension());
        std::ranges::transform(extension, extension.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        GltfOptions options;
        options.embedBuffers = extension != ".gltf";
        return WriteGltf(*static_cast<const Domain::Scene*>(payload), out, options);
    });
    exporters.Add("png", Domain::MediaKind::Image, [](const void* payload, const std::filesystem::path& out) {
        return WritePng(*static_cast<const Domain::Texture*>(payload), out);
    });
    exporters.Add("wav", Domain::MediaKind::Audio, [](const void* payload, const std::filesystem::path& out) {
        return WriteWav(*static_cast<const Domain::Audio*>(payload), out);
    });
}

}
