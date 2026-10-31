#include "Onyx/Render/CliVerbs.h"

#include <Onyx/Core/Cli/Exporters.h>
#include <Onyx/Core/Cli/Resolve.h>
#include <Onyx/Core/Cli/Run.h>
#include <Onyx/Core/Modules/DecodeContext.h>
#include <Onyx/Core/Modules/DecoderRegistry.h>
#include <Onyx/Gpu/Device.h>
#include <Onyx/Render/Camera.h>
#include <Onyx/Render/RenderToImage.h>

#include <charconv>
#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace Onyx::Render {

namespace {

constexpr std::string_view kUsageText =
    "<file> <entry|@path> --out <png> [--width N] [--height N] [--clip N --time S] [--grid] [--first] "
    "[--game <hint>]";
constexpr uint32_t kDefaultSize = 512;

struct RenderArgs {
    std::vector<std::string_view> positional;
    std::string_view out;
    std::string_view game;
    uint32_t width = kDefaultSize;
    uint32_t height = kDefaultSize;
    std::optional<uint32_t> clip;
    std::optional<float> time;
    bool grid = false;
    bool first = false;
};

template <class T>
bool ParseNumber(std::string_view text, T& into) {
    T value{};
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || end != text.data() + text.size()) return false;
    into = value;
    return true;
}

// Flags may appear anywhere after the verb. False for an unknown `--` token, a value flag with nothing
// after it, or a value that does not parse.
bool ParseRenderArgs(std::span<const std::string_view> args, RenderArgs& parsed) {
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string_view a = args[i];
        const bool hasValue = i + 1 < args.size();
        if (a == "--grid") {
            parsed.grid = true;
        } else if (a == "--first") {
            parsed.first = true;
        } else if (a == "--out" || a == "--game" || a == "--width" || a == "--height" || a == "--clip" ||
                   a == "--time") {
            if (!hasValue) return false;
            const std::string_view value = args[++i];
            if (a == "--out") {
                parsed.out = value;
            } else if (a == "--game") {
                parsed.game = value;
            } else if (a == "--width") {
                if (!ParseNumber(value, parsed.width)) return false;
            } else if (a == "--height") {
                if (!ParseNumber(value, parsed.height)) return false;
            } else if (a == "--clip") {
                uint32_t clip = 0;
                if (!ParseNumber(value, clip)) return false;
                parsed.clip = clip;
            } else {
                float time = 0.0f;
                if (!ParseNumber(value, time)) return false;
                parsed.time = time;
            }
        } else if (a.starts_with("--")) {
            return false;
        } else {
            parsed.positional.push_back(a);
        }
    }
    return true;
}

class OpenedDocument {
public:
    OpenedDocument(Modules::Workspace& ws, const std::filesystem::path& path, std::string_view hint) : m_ws(ws) {
        m_id = ws.Open(path, hint);
        if (m_id != 0) m_doc = ws.Get(m_id);
    }
    ~OpenedDocument() {
        if (m_id != 0) m_ws.Close(m_id);
    }
    OpenedDocument(const OpenedDocument&) = delete;
    OpenedDocument& operator=(const OpenedDocument&) = delete;

    Modules::Document* get() const { return m_doc; }

private:
    Modules::Workspace& m_ws;
    Modules::DocumentId m_id = 0;
    Modules::Document* m_doc = nullptr;
};

std::unique_ptr<Domain::Scene> DecodeScene(Modules::Workspace& ws, Modules::Document& doc,
                                           const Domain::AssetEntry& entry) {
    std::shared_ptr<const Vfs::IFile> file;
    if (entry.source.fileIndex < doc.files.size()) file = doc.files[entry.source.fileIndex];
    if (!file) return nullptr;
    Services::Progress progress;
    Modules::DecodeContext ctx{&doc,
                               entry,
                               Vfs::Reader(file, entry.source.offset, entry.source.size),
                               doc.diags,
                               progress,
                               ws.Settings(),
                               doc.moduleState};
    return ws.Decoders().DecodeScene(ctx);
}

// Frames the scene bounds from the +Z side.
View FrameFromFront(const Domain::Scene& scene, float aspect) {
    Domain::BoundingBox bounds = scene.bounds;
    if (bounds.Radius() <= 0.0f) bounds = scene.ComputeBounds();
    Camera camera;
    camera.FocusOn(bounds);
    camera.SnapToView(CameraView::Front);
    camera.UpdateAnimation(1.0f);
    View view;
    view.view = camera.ViewMatrix();
    view.proj = camera.Proj(aspect);
    return view;
}

int RunRender(Modules::Workspace& ws, const Cli::VerbTable& table, std::span<const std::string_view> argv,
              std::ostream& out, std::ostream& err) {
    RenderArgs args;
    if (!ParseRenderArgs(argv, args) || args.positional.size() != 2 || args.out.empty() || args.width == 0 ||
        args.height == 0) {
        err << "usage: render " << kUsageText << '\n';
        return Cli::kUsage;
    }

    const Cli::Exporter* png = table.Exporters().Find("png");
    if (!png) {
        err << "render.noPng: no png exporter registered\n";
        return Cli::kUsage;
    }

    const std::filesystem::path path(args.positional[0]);
    OpenedDocument opened(ws, path, args.game);
    if (!opened.get()) {
        out << "no module accepts " << path.string() << "\n";
        return Cli::kNoModule;
    }
    Modules::Document& doc = *opened.get();

    std::ostringstream why;
    const Domain::AssetEntry* entry = Cli::ResolveEntry(doc, args.positional[1], args.first, why);
    if (!entry) {
        const std::string reason = why.str();
        err << reason;
        if (!reason.starts_with("cli.ambiguous")) out << "unknown entry: " << args.positional[1] << "\n";
        return Cli::kUsage;
    }

    const std::unique_ptr<Domain::Scene> scene = DecodeScene(ws, doc, *entry);
    if (!scene) {
        out << "render.noScene: '" << entry->name << "' did not decode to a scene\n";
        return Cli::kPartial;
    }

    auto device = Gpu::Device::Create(Gpu::DeviceOptions{});
    if (!device.ok()) {
        out << device.error().code << ": " << device.error().message << "\n";
        return Cli::kPartial;
    }

    View view = FrameFromFront(*scene, static_cast<float>(args.width) / static_cast<float>(args.height));
    if (args.clip || args.time) view.pose = ClipTime{args.clip.value_or(0), args.time.value_or(0.0f)};
    Overlays overlays;
    overlays.grid = args.grid;

    auto image = RenderToImage(*device.value(), *scene, view, overlays, {args.width, args.height});
    if (!image.ok()) {
        out << image.error().code << ": " << image.error().message << "\n";
        return Cli::kPartial;
    }

    const std::filesystem::path to(args.out);
    auto written = png->write(&image.value(), to);
    if (!written.ok()) {
        out << "render failed: " << written.error().message << "\n";
        return Cli::kPartial;
    }
    out << "rendered " << entry->name << " to " << to.string() << " (" << args.width << "x" << args.height << ")\n";
    return Cli::kOk;
}

}

std::vector<Cli::Verb> CliVerbs() {
    return {Cli::Verb{"render", std::string(kUsageText), RunRender}};
}

}
