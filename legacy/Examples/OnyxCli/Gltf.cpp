#include <Onyx/Cli/Gltf.h>

// See Gltf.h's top comment for why this file exists separately from
// Commands.cpp/Onyx_Core, and compiles directly into the onyxbox-cli
// executable rather than into any static library -- the exact same
// placement rule Render.cpp follows for CmdRender/Onyx::Render.

#include <Onyx/Exchange/GltfExport.h>

namespace Onyx::Cli {

SceneExportFn MakeGltfExportFn(bool embedBuffers, bool includeSkin) {
    Exchange::GltfOptions opts;
    opts.embedBuffers = embedBuffers;
    opts.includeSkin = includeSkin;
    return [opts](const Parsers::SceneData& scene, const std::filesystem::path& out,
                  std::string& err) {
        return Exchange::ExportSceneData(scene, out, opts, err);
    };
}

} // namespace Onyx::Cli
