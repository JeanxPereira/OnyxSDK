#include "SelfTest.h"
#include "HexViewer.h"

#include <Onyx/App/Window.h>
#include <Onyx/App/App.h>
#include <Onyx/App/ViewerOpening.h>
#include <Onyx/App/ViewerRouting.h>
#include <Onyx/Services/Threading.h>
#include <Onyx/Services/Logger.h>
#include <Onyx/Vfs/OsFile.h>
#include <Onyx/Viewers/DocumentWindow.h>
#include <Onyx/Viewers/ImageViewer.h>
#include <Onyx/Modules/Selection.h>
#include <Onyx/Services/EventBus.h>

#include <OnyxBoxModule.h>

#include <cstring>
#include <memory>
#include <vector>
#include <cstdint>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

namespace {

// T10 proof helper (--open-first-image): depth-first search for the first
// AssetEntry this document's DecoderRegistry routes to the Image viewer --
// mirrors exactly what a user double-clicking that row in DocumentBrowser
// would trigger (Onyx::App::OpenSelection), just driven headlessly by a
// debug flag instead of a mouse click. Returns true and fills `outPath` on
// a match; false if nothing in the tree routes to Image.
bool FindFirstImageEntry(Onyx::Modules::Workspace& ws, const Onyx::Domain::AssetEntry& entry,
                         Onyx::Modules::NodePath& path, Onyx::Modules::NodePath& outPath) {
    if (Onyx::App::RouteForType(ws.Decoders(), entry.typeId) == Onyx::App::ViewerKind::Image) {
        outPath = path;
        return true;
    }
    for (uint32_t i = 0; i < entry.children.size(); ++i) {
        path.indices.push_back(i);
        if (FindFirstImageEntry(ws, entry.children[i], path, outPath)) return true;
        path.indices.pop_back();
    }
    return false;
}

} // namespace

static int RunGui(const char* optionalPath, bool uiTest, bool openFirstImage) {
    Onyx::Threading::MarkMainThread();

    Onyx::App::Window::initNative();
    Onyx::App::Window window;

    // Owned here (not inside the registrar lambda) so it outlives that
    // lambda's own call but still lives for the whole window.run() below --
    // exactly the one TreeReady event --open-first-image needs to react to.
    Onyx::Services::Subscription debugTreeReadySub;

    // If a file was passed, queue a hex tab once the App is initialised. The
    // registrar runs inside App::init(), after the engine's generic panels.
    std::string path = optionalPath ? optionalPath : "";
    window.app().SetRegistrar([&debugTreeReadySub, path, uiTest, openFirstImage](Onyx::App::App& app) {
        // OnyxBox (M3b): register the example module pre-init so the new
        // Workspace path can claim files it recognizes (e.g. .obx).
        app.AddModule(std::make_unique<OnyxBox::OnyxBoxModule>());

        // UI test mode: open the engine's widget/theme/icon gallery straight
        // away so the UI can be polished without loading any asset.
        if (uiTest) app.setPanelVisible("UI Gallery", true);
        if (path.empty()) return;

        // Open-at-boot convenience (M3b): route argv[1] through the
        // Workspace too, alongside the legacy hex tab below.
        Onyx::Modules::DocumentId openedId = app.GetWorkspace().OpenAsync(path);

        // --open-first-image (T10 debug flag): once this document's tree is
        // ready, open the first entry the DecoderRegistry routes to the
        // Image viewer -- the same OpenSelection() path a real double-click
        // in DocumentBrowser drives. The Subscription is owned by `window`
        // (see below) so it outlives this lambda and stays alive for the
        // one TreeReady event it needs.
        if (openFirstImage) {
            debugTreeReadySub =
                app.GetWorkspace().Events().On<Onyx::Modules::TreeReady>(
                    [&app, openedId](const Onyx::Modules::TreeReady& ev) {
                        if (ev.id != openedId || !ev.ok) return;

                        const Onyx::Modules::Document* doc = nullptr;
                        for (const auto& d : app.GetWorkspace().Documents()) {
                            if (d->id == ev.id) { doc = d.get(); break; }
                        }
                        if (!doc) {
                            LOG_ERR("[MinimalViewer] --open-first-image: document %llu not found "
                                     "after TreeReady", (unsigned long long)ev.id);
                            return;
                        }

                        Onyx::Modules::NodePath path;
                        Onyx::Modules::NodePath found;
                        bool matched = false;
                        for (uint32_t i = 0; i < doc->roots.size() && !matched; ++i) {
                            path.indices = {i};
                            matched = FindFirstImageEntry(app.GetWorkspace(), doc->roots[i], path, found);
                        }
                        if (!matched) {
                            LOG_WARN("[MinimalViewer] --open-first-image: no Image-routed entry "
                                     "found in document %llu", (unsigned long long)ev.id);
                            return;
                        }

                        Onyx::App::ViewerOpener opener;
                        opener.openImage = [&app](std::string name,
                                                  std::unique_ptr<Onyx::Parsers::TextureData> texture) {
                            app.getDocumentWindow().AddTab(
                                std::make_shared<Onyx::Viewers::ImageViewer>(name, std::move(texture)));
                            LOG_INFO("[MinimalViewer] --open-first-image: opened ImageViewer '%s'",
                                     name.c_str());
                        };

                        Onyx::App::OpenSelection(app.GetWorkspace(),
                                                 Onyx::Modules::SelectionChanged{doc->id, found}, opener);
                    });
        }

        Onyx::Vfs::OsFile file(path);
        if (!file.IsValid()) return;
        std::vector<uint8_t> bytes = file.ReadAll();
        constexpr size_t kMaxHexBytes = 64 * 1024; // demo hex view: cap to avoid OOM on large files
        if (bytes.size() > kMaxHexBytes) bytes.resize(kMaxHexBytes);
        auto viewer = std::make_shared<MinimalViewer::HexViewer>(path, std::move(bytes));
        app.getDocumentWindow().AddTab(viewer);
    });

    window.run();
    return 0;
}

int main(int argc, char** argv) {
    // Headless self-test: `MinimalViewer --selftest <file>` -> no window.
    if (argc >= 2 && std::strcmp(argv[1], "--selftest") == 0) {
        const char* path = (argc >= 3) ? argv[2] : argv[0]; // default: dump own exe
        return MinimalViewer::RunSelfTest(path);
    }

    // `MinimalViewer --ui-test [file]` opens with the UI Gallery already up.
    // `MinimalViewer --open-first-image [file]` (T10): once the file's tree
    // is ready, programmatically opens the first entry that routes to the
    // Image viewer -- proof that ImageViewer draws a frame with a valid
    // Vulkan ImTextureID, for a timeout-run smoke check with no manual
    // double-click involved.
    bool uiTest = false;
    bool openFirstImage = false;
    const char* optionalPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ui-test") == 0)
            uiTest = true;
        else if (std::strcmp(argv[i], "--open-first-image") == 0)
            openFirstImage = true;
        else if (!optionalPath)
            optionalPath = argv[i];
    }
    return RunGui(optionalPath, uiTest, openFirstImage);
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return main(__argc, __argv);
}
#endif
