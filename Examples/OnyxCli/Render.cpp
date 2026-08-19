#include <Onyx/Cli/Render.h>

// See Render.h's top comment for why this file exists separately from
// Commands.cpp/Onyx_Core, and compiles directly into the onyxbox-cli
// executable rather than into any static library.

#include <Onyx/Domain/Entry.h>
#include <Onyx/Modules/DecoderRegistry.h>
#include <Onyx/Parsers/SceneNode.h>
#include <Onyx/Services/Diagnostics.h>
#include <Onyx/Services/Jobs.h>

#include <Onyx/Rendering/ShaderManager.h> // Rendering::ShadingMode
#include <Onyx/RenderVk/OffscreenTarget.h>
#include <Onyx/RenderVk/Pipelines.h>      // Onyx::RenderVk::VulkanProjection, ScenePipelines
#include <Onyx/RenderVk/SceneRendererVk.h>
#include <Onyx/RenderVk/VkContext.h>
#include <Onyx/RenderVk/VkResources.h>    // Resources::OneShot

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// stb_image_write ODR note (task brief's explicit concern): this macro is
// defined exactly once, in this one TU. Render.cpp compiles ONLY into the
// onyxbox-cli executable (Examples/OnyxCli/CMakeLists.txt lists it as a
// source directly, not via any static library another target could also
// link) and is NOT one of Tests/CMakeLists.txt's onyx_tests sources -- so
// this definition never lands in more than one link step. Tools/OnyxOracle/
// PngWrite.cpp defines the SAME macro in its own TU, but onyx-oracle is a
// separate executable that never links onyxbox-cli (or vice versa), so the
// two definitions never collide at link time either. (Checked: `grep -rn
// STB_IMAGE_WRITE_IMPLEMENTATION` before adding this turned up exactly one
// prior definition, Tools/OnyxOracle/PngWrite.cpp.)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <ostream>
#include <string>
#include <system_error>
#include <vector>

namespace Onyx::Cli {

using Modules::DecodeContext;
using Modules::DecoderRegistry;
using Modules::Document;
using Modules::DocumentId;
using Modules::Workspace;
using Services::Diag;
using Services::Progress;
using Services::Severity;

namespace {

// Duplicated from Commands.cpp's own private FindEntryByName (first-name
// match, pre-order depth-first search) rather than exported from
// Commands.h: exporting it would make Onyx_Core's public Cli surface
// responsible for a helper only this file (compiled into the executable,
// never into Onyx_Core -- see Render.h's top comment) also needs. A
// ~10-line duplication is cheaper than a shared internal header for one
// small function used by exactly two call sites in the whole codebase.
const Domain::AssetEntry* FindEntryByName(const std::vector<Domain::AssetEntry>& entries,
                                           std::string_view name) {
    for (const auto& e : entries) {
        if (e.name == name) return &e;
        if (const auto* found = FindEntryByName(e.children, name)) return found;
    }
    return nullptr;
}

// Minimal JSON string escaping -- duplicated from Commands.cpp's own
// private JsonEscape for the same reason as FindEntryByName above.
std::string JsonEscape(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += char(c);
                }
                break;
        }
    }
    return out;
}

// Writes tightly packed top-down RGBA as a PNG via stb_image_write.
// Deliberately NOT Tools/OnyxOracle/PngWrite.h -- that header lives in the
// oracle tool, which Source/Cli (and the onyxbox-cli executable it compiles
// into) never links; see this file's ODR comment at the top for the
// STB_IMAGE_WRITE_IMPLEMENTATION shape this duplication needed.
bool WritePng(const std::filesystem::path& path, int width, int height,
              const std::vector<uint8_t>& rgba, std::string& err) {
    if (rgba.size() < static_cast<size_t>(width) * static_cast<size_t>(height) * 4) {
        err = "pixel buffer smaller than the declared frame";
        return false;
    }
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    if (!stbi_write_png(path.string().c_str(), width, height, 4, rgba.data(), width * 4)) {
        err = "stbi_write_png failed for " + path.string();
        return false;
    }
    return true;
}

const char* SeverityLabel(Severity s) {
    switch (s) {
        case Severity::Error:   return "Error";
        case Severity::Warning: return "Warning";
        default:                return "Info";
    }
}

bool AllPixelsEqual(const std::vector<uint8_t>& rgba, const uint8_t expected[4]) {
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        if (rgba[i + 0] != expected[0] || rgba[i + 1] != expected[1] || rgba[i + 2] != expected[2] ||
            rgba[i + 3] != expected[3]) {
            return false;
        }
    }
    return true;
}

} // namespace

int CmdRender(Workspace& ws, const std::filesystem::path& path, std::string_view entryName,
              const std::filesystem::path& outPng, int width, int height, std::ostream& out,
              std::string_view moduleHint) {
    if (width <= 0) width = 512;
    if (height <= 0) height = 512;

    DocumentId id = ws.Open(path, moduleHint);
    if (id == 0) {
        out << "no module accepts " << path.string() << "\n";
        return kNoModule;
    }

    Document* doc = ws.Get(id);
    const Domain::AssetEntry* entry = FindEntryByName(doc->roots, entryName);
    if (!entry) {
        out << "unknown entry: " << entryName << "\n";
        ws.Close(id);
        return kUsage;
    }

    DecoderRegistry& reg = ws.Decoders();
    if (!reg.HasScene(entry->typeId)) {
        out << "no scene decoder for entry: " << entryName << "\n";
        ws.Close(id);
        return kUsage;
    }

    Progress progress;
    DecodeContext ctx{*doc, *entry, doc->diags, progress};
    std::unique_ptr<Parsers::SceneData> scene = reg.DecodeScene(ctx);
    if (!scene) {
        out << "scene decode failed for entry: " << entryName << " (see diagnostics below)\n";
        for (const Diag& d : doc->diags.Drain()) {
            out << "[" << SeverityLabel(d.severity) << "] " << d.code << ": " << d.message << "\n";
        }
        ws.Close(id);
        return kUsage;
    }
    const std::string entryNameCopy(entryName); // outlives ws.Close(id) below

    ws.Close(id); // scene is self-contained CPU data; no reference back into doc/ws survives this

    // ── camera: frame the decoded scene's object-space bbox with margin ──
    // Hand-rolled here rather than reusing Onyx::Rendering::Camera
    // (Include/Onyx/Rendering/Camera.h): that class is pure glm math with
    // no GL dependency of its own, but it compiles into Onyx_Render
    // (ONYX_RENDER_SOURCES, CMakeLists.txt) alongside GpuMesh.cpp/
    // SceneRenderer.cpp, which DO pull in glad + the platform GL lib
    // (opengl32 on Windows). Linking Onyx::Render into onyxbox-cli just to
    // reuse ~10 lines of Camera::FocusOn/UpdatePosition math would drag a
    // whole unused GL toolchain dependency into a Vulkan-only headless CLI
    // -- this mirrors Camera::FocusOn's exact formula (distance =
    // radius/sin(halfFov/2)*1.2 margin, fixed 45deg/15deg orbit) without
    // that link.
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(-std::numeric_limits<float>::max());
    bool anyVertex = false;
    for (const Parsers::MeshPart& part : scene->meshParts) {
        for (const Domain::GpuVertex& v : part.vertices) {
            lo = glm::min(lo, v.position);
            hi = glm::max(hi, v.position);
            anyVertex = true;
        }
    }
    if (!anyVertex) { lo = hi = glm::vec3(0.0f); }
    const glm::vec3 center = (lo + hi) * 0.5f;
    const float radius = std::max(glm::length(hi - lo) * 0.5f, 1.0f);

    constexpr float kFovYDeg = 55.0f; // matches Rendering::Camera::fov's own default
    const float halfFovRad = glm::radians(kFovYDeg * 0.5f);
    const float distance = std::max(radius / std::sin(halfFovRad) * 1.2f, 0.1f); // 1.2x margin

    const float yaw = glm::radians(45.0f);   // same fixed 3/4-isometric orbit Camera::FocusOn snaps to
    const float pitch = glm::radians(15.0f);
    const glm::vec3 eye = center + distance * glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                                                          std::cos(pitch) * std::cos(yaw));
    const glm::mat4 view = glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));

    const float aspect = float(width) / float(height);
    const float nearPlane = std::max(distance * 0.002f, 0.01f);
    const float farPlane = distance + radius * 2.2f + 1.0f;
    glm::mat4 proj = glm::perspective(glm::radians(kFovYDeg), aspect, nearPlane, farPlane);
    // Every Vulkan draw in this codebase must route its projection through
    // this before handing it to SceneRendererVk::Render() -- see Include/
    // Onyx/RenderVk/Pipelines.h's "Camera convention" note; Render() itself
    // asserts (Debug builds) that this was not forgotten.
    proj = Onyx::RenderVk::VulkanProjection(proj);

    // ── headless Vulkan boot ──────────────────────────────────────────────
    Onyx::RenderVk::VkContext vkCtx;
    std::string vkErr;
    if (!vkCtx.Init(/*presentSupport=*/false, vkErr)) {
        out << "no Vulkan device: " << vkErr << "\n";
        return 77; // tool convention (Tools/OnyxOracle's Vk* entry points): SKIP, not FAIL
    }

    Onyx::RenderVk::ScenePipelines scenePipes;
    if (!Onyx::RenderVk::Pipelines::CreateScene(vkCtx, scenePipes, vkErr)) {
        out << "render: " << vkErr << "\n";
        vkCtx.Shutdown();
        return kUsage;
    }

    Onyx::RenderVk::OffscreenTarget target;
    if (!target.Create(vkCtx, width, height, vkErr)) {
        out << "render: " << vkErr << "\n";
        Onyx::RenderVk::Pipelines::Destroy(vkCtx, scenePipes);
        vkCtx.Shutdown();
        return kUsage;
    }

    Onyx::RenderVk::SceneRendererVk renderer;
    if (!renderer.Build(vkCtx, scenePipes, *scene, vkErr)) {
        out << "render: " << vkErr << "\n";
        renderer.Clear(vkCtx);
        target.Destroy(vkCtx);
        Onyx::RenderVk::Pipelines::Destroy(vkCtx, scenePipes);
        vkCtx.Shutdown();
        return kUsage;
    }

    // Same neutral clear color Tools/OnyxOracle/Main.cpp's render-corpus
    // --renderer vk path uses -- not app-config dependent (this tool has
    // no AppConfig instance).
    const float clearColor[4] = {0.10f, 0.11f, 0.13f, 1.0f};
    std::vector<uint8_t> rgba;
    bool ok = Onyx::RenderVk::Resources::OneShot(vkCtx, [&](VkCommandBuffer cmd) {
        target.BeginFrame(cmd, clearColor);
        renderer.Render(cmd, view, proj, Onyx::Rendering::ShadingMode::Solid, width, height);
        target.EndFrame(cmd);
    }, vkErr);
    if (ok) ok = target.Readback(vkCtx, rgba, vkErr);

    renderer.Clear(vkCtx);
    target.Destroy(vkCtx);
    Onyx::RenderVk::Pipelines::Destroy(vkCtx, scenePipes);

    if (!ok) {
        out << "render: " << vkErr << "\n";
        vkCtx.Shutdown();
        return kUsage;
    }

    // A frame that is nothing but the clear color means geometry never
    // rasterized -- treat that as a render failure rather than silently
    // writing an empty PNG. (This is also what "is non-uniform" resolves
    // to for this command's own ctest -- see Examples/OnyxCli/
    // RenderTest.cmake's top comment.)
    const uint8_t clearBytes[4] = {
        static_cast<uint8_t>(clearColor[0] * 255.0f + 0.5f),
        static_cast<uint8_t>(clearColor[1] * 255.0f + 0.5f),
        static_cast<uint8_t>(clearColor[2] * 255.0f + 0.5f),
        static_cast<uint8_t>(clearColor[3] * 255.0f + 0.5f),
    };
    if (AllPixelsEqual(rgba, clearBytes)) {
        out << "render: every pixel equals the clear color -- geometry did not rasterize\n";
        vkCtx.Shutdown();
        return kUsage;
    }

    std::string pngErr;
    if (!WritePng(outPng, width, height, rgba, pngErr)) {
        out << "render: " << pngErr << "\n";
        vkCtx.Shutdown();
        return kUsage;
    }

    size_t totalVertices = 0;
    for (const Parsers::MeshPart& part : scene->meshParts) totalVertices += part.vertices.size();

    // Report JSON beside the PNG -- a minimal, hand-authored shape (entry
    // name, dimensions, part/material/vertex counts), NOT Tools/OnyxOracle/
    // RenderReport.h's BuildReport(). See Render.h's top comment for why:
    // that function's byte-stable output is pinned to GL/Vulkan pixel-
    // parity testing (Rendering::RenderBatch, a per-batch GL-texture-id
    // sentinel scheme -- SceneRendererVk.h's own top comment), a concern
    // this generic CLI command has no business depending on, and it lives
    // in a tool-side header this executable does not link.
    std::filesystem::path jsonPath = outPng;
    jsonPath.replace_extension(".json");
    std::ofstream jf(jsonPath, std::ios::binary | std::ios::trunc);
    if (jf) {
        jf << "{\n"
           << "  \"entry\": \"" << JsonEscape(entryNameCopy) << "\",\n"
           << "  \"width\": " << width << ",\n"
           << "  \"height\": " << height << ",\n"
           << "  \"parts\": " << scene->meshParts.size() << ",\n"
           << "  \"materials\": " << scene->materials.size() << ",\n"
           << "  \"vertices\": " << totalVertices << "\n"
           << "}\n";
    }

    out << "rendered " << entryNameCopy << " " << width << "x" << height
        << " parts=" << scene->meshParts.size() << " materials=" << scene->materials.size()
        << " vertices=" << totalVertices << " -> " << outPng.string() << "\n";

    vkCtx.Shutdown();
    return kOk;
}

} // namespace Onyx::Cli
