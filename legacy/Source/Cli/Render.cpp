#include <Onyx/Cli/Render.h>

// See Render.h's top comment for the full history of where this file has
// lived (M4: Examples/OnyxCli/, compiled straight into onyxbox-cli; M5
// Task 6: here, compiled into Onyx_CliRender, a small static library
// above both Onyx_Core and Onyx_Render -- root CMakeLists.txt's
// ONYX_CLIRENDER_SOURCES/`Onyx::CliRender` target). That target links
// Onyx_Core and Onyx_Render PUBLIC, and nothing links back into it from
// either side, so the cycle Render.h's top comment explains simply cannot
// form -- this file is free to include Vulkan/glm/Render-layer headers
// exactly like it always could, just from a linkable static library
// instead of an executable's own source list.
//
// Why shape (a) -- a shipped library owning this file -- rather than
// shape (b) -- an injected render callback the composition root supplies,
// the shape Include/Onyx/Cli/Gltf.h's MakeGltfExportFn/SceneExportFn use
// for `decode --to gltf`: the audit finding this fixes (G1,
// docs/design/2026-08-19-public-surface-audit.md) is specifically that
// `CmdRender` -- a symbol this PUBLIC header declares -- ships in no
// library. Shape (b) alone would not have fixed that: SceneExportFn's own
// implementation (Onyx::Exchange::ExportSceneData wrapped in a lambda)
// still only exists inside Examples/OnyxCli/Gltf.cpp, compiled straight
// into onyxbox-cli -- a third party gets the HOOK type from Commands.h but
// still has to write their own exporter body, because Commands.h never
// promised a working default. Render.h, by contrast, promises a WORKING
// `CmdRender` (full doc comment, concrete parameter list, concrete return
// codes) -- a promise only a real shipped implementation keeps. Hence
// shape (a) for this symbol specifically. `Onyx::Cli::Run()`'s "render"
// argv dispatch (Source/Cli/Commands.cpp) still uses shape (b) for the
// VERB, because Run() itself lives in Onyx_Core and must stay Vulkan-free
// -- see Commands.h's RenderFn doc comment for how the two compose.

#include <Onyx/Domain/Entry.h>
#include <Onyx/Modules/DecoderRegistry.h>
#include <Onyx/Parsers/SceneNode.h>
#include <Onyx/Services/Diagnostics.h>
#include <Onyx/Services/Jobs.h>

// M5 Task 7: this file used to include OffscreenTarget.h/Pipelines.h/
// SceneRendererVk.h/VkResources.h directly and hand-roll the pipeline-
// create/target-create/Build/OneShot-render/Readback/cleanup sequence
// those headers exist for -- RenderToImage.h's context-reusing overload
// now owns all of that (see its own top comment for the full contract);
// only VkContext.h stays, since this command still needs vkCtx.Init()
// itself to detect "no device" and return 77, a CLI-exit-code concern
// RenderToImage has no business encoding.
#include <Onyx/Rendering/RenderToImage.h>
#include <Onyx/Rendering/VkContext.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// stb_image_write ODR note (task brief's explicit concern, still live
// after the M5 Task 6 move): this macro is defined exactly once, in this
// one TU. Render.cpp compiles into Onyx_CliRender (root CMakeLists.txt's
// ONYX_CLIRENDER_SOURCES), a STATIC library -- the actual ODR question is
// therefore "does any final link step pull in a second TU that also
// defines STB_IMAGE_WRITE_IMPLEMENTATION", not "does any other target
// compile this file". Tools/OnyxOracle/PngWrite.cpp defines the SAME
// macro in its own TU, but onyx-oracle is a separate executable that
// links neither onyxbox-cli nor Onyx_CliRender (and vice versa), and
// Tests/CMakeLists.txt's onyx_tests does not link Onyx_CliRender either
// (Tests/cli_test.cpp exercises Run()'s render argv-dispatch through a
// stub RenderFn, never the real CmdRender -- see that file's own comment
// for why: a real render needs a Vulkan device, which is why the actual
// GPU proof lives in Examples/OnyxCli/Render*Test.cmake's device-gated
// ctest scripts instead, same convention as every onyx-oracle Vk* ctest).
// So the two STB_IMAGE_WRITE_IMPLEMENTATION definitions still never
// collide at link time. (Checked: `grep -rn STB_IMAGE_WRITE_IMPLEMENTATION`
// before this file's M4 addition turned up exactly one prior definition,
// Tools/OnyxOracle/PngWrite.cpp; nothing since has added a third.)
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
// responsible for a helper only this file (compiled into Onyx_CliRender,
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
// oracle tool, which Onyx_CliRender never links; see this file's ODR
// comment at the top for the STB_IMAGE_WRITE_IMPLEMENTATION shape this
// duplication needed.
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

void PrintDiags(std::ostream& out, const std::vector<Diag>& diags) {
    for (const Diag& d : diags) {
        out << "[" << SeverityLabel(d.severity) << "] " << d.code << ": " << d.message << "\n";
    }
}

// Duplicated from Commands.cpp's own private AnyError for the same reason
// as FindEntryByName/JsonEscape above -- this is `render --strict`'s own
// check, mirroring `decode --strict`'s exactly (Commands.cpp's CmdDecode).
bool AnyErrorDiag(const std::vector<Diag>& diags) {
    for (const Diag& d : diags) {
        if (d.severity == Severity::Error) return true;
    }
    return false;
}

// ── canonical views (spec §11, M5 Task 6) ──────────────────────────────────
// Each name maps to a fixed (yaw, pitch) orbit around the same object-space
// bbox center/distance every view shares (computed once per render, below)
// -- only the camera's angle around that pivot changes. "iso" is the exact
// 45deg-yaw/15deg-pitch orbit `render` used before --views existed, so it
// stays the default and reproduces M4's output byte-for-byte. "top" uses
// 89deg rather than a literal 90: at exactly 90 the eye direction (0,1,0)
// is parallel to the up vector glm::lookAt uses below, which degenerates
// the view matrix (a zero-length cross product) -- 89deg keeps the look
// direction a hair off vertical, visually indistinguishable from straight
// down at any of this command's framing distances, with no special-cased
// up vector needed.
struct ViewAngles {
    std::string_view name;
    float yawDeg;
    float pitchDeg;
};
constexpr ViewAngles kViewAngles[] = {
    {"iso",    45.0f, 15.0f},
    {"front",   0.0f,  0.0f},
    {"back",  180.0f,  0.0f},
    {"left",  -90.0f,  0.0f},
    {"right",  90.0f,  0.0f},
    {"top",     0.0f, 89.0f},
};

// T6-review rider: a SIZE-only check (the two arrays merely have the same
// element count) compiles clean even when a name is renamed in one list but
// not the other -- the usage text (kCanonicalViews) would still advertise a
// name FindView() below can no longer resolve, surfacing only at runtime as
// an "unknown view" for a name the --help output still shows. Check the
// correspondence name-for-name instead, so a drift between this array and
// Commands.h's kCanonicalViews is a compile error, not a runtime surprise.
constexpr bool ViewNamesMatchCanonical() {
    if (sizeof(kViewAngles) / sizeof(kViewAngles[0]) != kCanonicalViews.size()) return false;
    for (size_t i = 0; i < kCanonicalViews.size(); ++i) {
        if (kViewAngles[i].name != kCanonicalViews[i]) return false;
    }
    return true;
}
static_assert(ViewNamesMatchCanonical(),
              "kViewAngles must define, in the same order, exactly the names in "
              "Commands.h's kCanonicalViews -- a size match alone is not enough");

const ViewAngles* FindView(std::string_view name) {
    for (const ViewAngles& v : kViewAngles) {
        if (v.name == name) return &v;
    }
    return nullptr;
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
              std::string_view moduleHint, std::string_view view, bool strict) {
    if (width <= 0) width = 512;
    if (height <= 0) height = 512;

    const ViewAngles* viewAngles = FindView(view);
    if (!viewAngles) {
        out << "render: unknown view '" << view << "' (expected one of:";
        for (std::string_view v : kCanonicalViews) out << " " << v;
        out << ")\n";
        return kUsage;
    }

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
    // Drained once regardless of outcome: a failed decode prints these as
    // its own explanation (unchanged from before --strict existed); a
    // successful decode also prints them (new -- matches CmdDecode's own
    // always-print-diags shape) so `--strict`'s exit code has a visible
    // reason attached, not just a bare exit 3.
    std::vector<Diag> diags = doc->diags.Drain();
    if (!scene) {
        out << "scene decode failed for entry: " << entryName << " (see diagnostics below)\n";
        PrintDiags(out, diags);
        ws.Close(id);
        return kUsage;
    }
    PrintDiags(out, diags);
    const bool strictFail = strict && AnyErrorDiag(diags);
    const std::string entryNameCopy(entryName); // outlives ws.Close(id) below

    ws.Close(id); // scene is self-contained CPU data; no reference back into doc/ws survives this

    // ── camera: frame the decoded scene's object-space bbox with margin ──
    // Hand-rolled here rather than reusing Onyx::Rendering::Camera
    // (Include/Onyx/Rendering/Camera.h). Originally this was to dodge a GL
    // link (Camera.cpp compiled into the GL Onyx_Render alongside
    // GpuMesh.cpp/SceneRenderer.cpp, which pulled in glad + opengl32) --
    // Task 11 deleted that GL renderer and Camera.cpp now compiles into the
    // Vulkan-only Render layer with no GL dependency at all, so that
    // specific reason is gone. Reusing Camera here regardless is a
    // reasonable follow-up but is out of Task 11's own file list/scope;
    // left hand-rolled for now, close to but NOT byte-identical to
    // Camera::FocusOn's formula (Source/Rendering/Camera.cpp:116-127): this
    // block clamps `radius` itself to >= 1.0 before dividing by
    // sin(halfFov), where FocusOn divides the RAW (unclamped) bbox radius
    // and only clamps the final `distance` to >= 0.1 -- the two formulas
    // agree once radius >= 1.0 (the common case) and diverge only for a
    // sub-unit bbox, where this CLI's version sits farther back than
    // FocusOn would. Stated honestly rather than silently left claiming an
    // "exact" match it was never quite making.
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

    // `view` (validated above) selects the fixed orbit angle around this
    // same center/distance pivot -- see kViewAngles' own doc comment.
    const float yaw = glm::radians(viewAngles->yawDeg);
    const float pitch = glm::radians(viewAngles->pitchDeg);
    const glm::vec3 eye = center + distance * glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                                                          std::cos(pitch) * std::cos(yaw));
    const glm::mat4 view_ = glm::lookAt(eye, center, glm::vec3(0.0f, 1.0f, 0.0f));

    const float aspect = float(width) / float(height);
    const float nearPlane = std::max(distance * 0.002f, 0.01f);
    const float farPlane = distance + radius * 2.2f + 1.0f;
    const glm::mat4 proj = glm::perspective(glm::radians(kFovYDeg), aspect, nearPlane, farPlane);
    // NOT VulkanProjection()-converted here -- Onyx::Rendering::RenderToImage
    // (called below) applies that Y-flip internally exactly once. See
    // RenderToImage.h's "the projection-convention decision" for the full
    // contract and why a plain matrix is what this API wants.

    // ── headless Vulkan boot ──────────────────────────────────────────────
    Onyx::Rendering::VkContext vkCtx;
    std::string vkErr;
    if (!vkCtx.Init(/*presentSupport=*/false, vkErr)) {
        out << "no Vulkan device: " << vkErr << "\n";
        return 77; // tool convention (Tools/OnyxOracle's Vk* entry points): SKIP, not FAIL
    }

    // M5 Task 7: RenderToImage's context-reusing overload owns pipeline
    // creation, target creation, SceneRendererVk::Build, the OneShot Render
    // call, Readback, and unwinding all of it -- this file no longer spells
    // any of that out by hand. `mode` stays the default (Solid, matching
    // this command's pre-existing behavior exactly -- render never exposed
    // a --shading flag); no background is requested (also pre-existing:
    // render's own frame was always a flat clear color, never a gradient --
    // clearColor below reproduces that exact neutral gray, same value
    // Tools/OnyxOracle/Main.cpp's render-corpus --renderer vk path uses,
    // not app-config dependent since this tool has no AppConfig instance).
    Onyx::Rendering::RenderRequest request{*scene, width, height, view_, proj};
    request.clearColor = glm::vec4(0.10f, 0.11f, 0.13f, 1.0f);
    std::vector<uint8_t> rgba;
    bool ok = Onyx::Rendering::RenderToImage(vkCtx, request, rgba, vkErr);

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
        static_cast<uint8_t>(request.clearColor.r * 255.0f + 0.5f),
        static_cast<uint8_t>(request.clearColor.g * 255.0f + 0.5f),
        static_cast<uint8_t>(request.clearColor.b * 255.0f + 0.5f),
        static_cast<uint8_t>(request.clearColor.a * 255.0f + 0.5f),
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
    // name, view, dimensions, part/material/vertex counts), NOT
    // Tools/OnyxOracle/RenderReport.h's BuildReport(). See Render.h's top
    // comment for why: that function's byte-stable output is pinned to
    // GL/Vulkan pixel-parity testing (Rendering::RenderBatch, a per-batch
    // GL-texture-id sentinel scheme -- SceneRendererVk.h's own top
    // comment), a concern this generic CLI command has no business
    // depending on, and it lives in a tool-side header this library does
    // not link.
    std::filesystem::path jsonPath = outPng;
    jsonPath.replace_extension(".json");
    std::ofstream jf(jsonPath, std::ios::binary | std::ios::trunc);
    if (jf) {
        jf << "{\n"
           << "  \"entry\": \"" << JsonEscape(entryNameCopy) << "\",\n"
           << "  \"view\": \"" << JsonEscape(view) << "\",\n"
           << "  \"width\": " << width << ",\n"
           << "  \"height\": " << height << ",\n"
           << "  \"parts\": " << scene->meshParts.size() << ",\n"
           << "  \"materials\": " << scene->materials.size() << ",\n"
           << "  \"vertices\": " << totalVertices << "\n"
           << "}\n";
    }

    out << "rendered " << entryNameCopy << " view=" << view << " " << width << "x" << height
        << " parts=" << scene->meshParts.size() << " materials=" << scene->materials.size()
        << " vertices=" << totalVertices << " -> " << outPng.string() << "\n";

    vkCtx.Shutdown();
    return strictFail ? kStrictErrors : kOk;
}

} // namespace Onyx::Cli
