#include "SelfTest.h"
#include "HexViewer.h"

#include <Onyx/App/Window.h>
#include <Onyx/App/App.h>
#include <Onyx/App/IPanel.h>
#include <Onyx/App/ViewerOpening.h>
#include <Onyx/App/ViewerRouting.h>
#include <Onyx/Services/Threading.h>
#include <Onyx/Services/Logger.h>
#include <Onyx/Vfs/OsFile.h>
#include <Onyx/Viewers/DocumentWindow.h>
#include <Onyx/Viewers/ImageViewer.h>
#include <Onyx/Viewers/Viewport3D.h>
#include <Onyx/Modules/Selection.h>
#include <Onyx/Services/EventBus.h>

#include <OnyxBoxModule.h>

#include <cstring>
#include <memory>
#include <string>
#include <string_view>
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

// T14 proof helper (--open-first-scene): same shape as FindFirstImageEntry
// above, routed to Scene instead of Image -- depth-first search for the
// first AssetEntry the DecoderRegistry routes to the Scene viewer.
bool FindFirstSceneEntry(Onyx::Modules::Workspace& ws, const Onyx::Domain::AssetEntry& entry,
                         Onyx::Modules::NodePath& path, Onyx::Modules::NodePath& outPath) {
    if (Onyx::App::RouteForType(ws.Decoders(), entry.typeId) == Onyx::App::ViewerKind::Scene) {
        outPath = path;
        return true;
    }
    for (uint32_t i = 0; i < entry.children.size(); ++i) {
        path.indices.push_back(i);
        if (FindFirstSceneEntry(ws, entry.children[i], path, outPath)) return true;
        path.indices.pop_back();
    }
    return false;
}

// T14 proof panel (--open-first-scene): a no-visual, logic-only IPanel
// (Draw() issues no ImGui:: calls, exactly like DocumentBrowser.cpp's
// DecodingPlaceholder is a no-op-looking IDocumentContent for the same
// reason) that watches a Viewport3D pointer set via SetTarget() and logs
// exactly once, the first frame its DisplayTexture() is no longer
// ImTextureID_Invalid -- proof that a real Vulkan-backed Viewport3D
// actually rendered a scene and registered a sample-able texture, driven
// headlessly from a debug flag instead of a human watching the window.
// PanelRegistry::DrawAll() (Include/Onyx/App/PanelRegistry.h) calls Draw()
// on every registered visible panel unconditionally, once per frame, which
// is exactly the polling this needs -- no separate per-frame hook exists
// (or is needed) elsewhere in App's public surface.
//
// weak_ptr, NOT shared_ptr (caught live, see this task's report): App's
// member order destroys m_panels AFTER m_documentWindow (Include/Onyx/App/
// App.h), and Window::exitVulkan() only runs after
// m_app.getDocumentWindow().Shutdown() has torn down every open tab's own
// Vulkan resources WHILE the VkContext is still alive (T10's own hang fix
// -- see Source/App/Window.cpp's ~Window() comment). A shared_ptr copy
// held here would keep the Viewport3D's refcount above zero past
// DocumentWindow::Shutdown()'s m_tabs.clear(), delaying its real
// destructor (which destroys VMA-owned images/buffers) until m_panels is
// torn down -- AFTER exitVulkan() already destroyed the device/allocator.
// That reproduced exactly the "process alive, CPU climbing, graceful
// taskkill never closing it" symptom Task 10's report describes for the
// same underlying reason. A weak_ptr observes without extending the
// tab's lifetime, so this panel goes back to reporting nothing (target
// gone) the instant DocumentWindow actually releases it -- which is
// correct: there is nothing left to log once the tab is closed.
class SceneDrawProofPanel : public Onyx::App::IPanel {
public:
    void SetTarget(std::shared_ptr<Onyx::Viewers::Viewport3D> vp) { m_target = vp; }

    void Draw() override {
        if (m_logged) return;
        auto vp = m_target.lock();
        if (!vp) return;
        ImTextureID tex = vp->DisplayTexture();
        if (tex != ImTextureID_Invalid) {
            LOG_INFO("[MinimalViewer] --open-first-scene: Viewport3D drew a frame with a valid "
                     "ImTextureID (%llu)", (unsigned long long)tex);
            m_logged = true;
        }
    }
    std::string_view getName() const override { return "##SceneDrawProof"; }

private:
    std::weak_ptr<Onyx::Viewers::Viewport3D> m_target;
    bool m_logged = false;
};

} // namespace

static int RunGui(const char* optionalPath, bool uiTest, bool openFirstImage, bool openFirstScene) {
    Onyx::Threading::MarkMainThread();

    Onyx::App::Window::initNative();
    Onyx::App::Window window;

    // Owned here (not inside the registrar lambda) so it outlives that
    // lambda's own call but still lives for the whole window.run() below --
    // exactly the one TreeReady event --open-first-image/--open-first-scene
    // each needs to react to. Two separate subscriptions (not one shared)
    // since both flags may be passed together and each owns its own
    // one-shot lifetime independently.
    Onyx::Services::Subscription debugTreeReadySub;
    Onyx::Services::Subscription debugTreeReadySceneSub;
    // Owned here for the same reason as the two subscriptions above: the
    // hex-tab fallback (see below) needs to react to this document's own
    // TreeReady, one shot, and must outlive the registrar lambda to do it.
    Onyx::Services::Subscription hexFallbackSub;

    // If a file was passed, queue a hex tab once the App is initialised. The
    // registrar runs inside App::init(), after the engine's generic panels.
    std::string path = optionalPath ? optionalPath : "";
    window.app().SetRegistrar([&debugTreeReadySub, &debugTreeReadySceneSub, &hexFallbackSub, path, uiTest,
                                openFirstImage, openFirstScene](Onyx::App::App& app) {
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
                        opener.openImage = [&app](Onyx::Modules::DocumentId doc, std::string name,
                                                  std::unique_ptr<Onyx::Parsers::TextureData> texture) {
                            app.getDocumentWindow().AddTab(
                                std::make_shared<Onyx::Viewers::ImageViewer>(name, std::move(texture)), doc);
                            LOG_INFO("[MinimalViewer] --open-first-image: opened ImageViewer '%s'",
                                     name.c_str());
                        };

                        Onyx::App::OpenSelection(app.GetWorkspace(),
                                                 Onyx::Modules::SelectionChanged{doc->id, found}, opener);
                    });
        }

        // --open-first-scene (T14 debug flag): same pattern as
        // --open-first-image above, routed to the Scene viewer instead.
        // The proof panel is registered up front (always present, harmless
        // no-op until SetTarget() is called) so opener.openScene below just
        // has to hand it the freshly created Viewport3D.
        if (openFirstScene) {
            auto proofPanel = std::make_unique<SceneDrawProofPanel>();
            SceneDrawProofPanel* proofPanelPtr = proofPanel.get();
            app.addPanel(std::move(proofPanel));

            debugTreeReadySceneSub =
                app.GetWorkspace().Events().On<Onyx::Modules::TreeReady>(
                    [&app, openedId, proofPanelPtr](const Onyx::Modules::TreeReady& ev) {
                        if (ev.id != openedId || !ev.ok) return;

                        const Onyx::Modules::Document* doc = nullptr;
                        for (const auto& d : app.GetWorkspace().Documents()) {
                            if (d->id == ev.id) { doc = d.get(); break; }
                        }
                        if (!doc) {
                            LOG_ERR("[MinimalViewer] --open-first-scene: document %llu not found "
                                     "after TreeReady", (unsigned long long)ev.id);
                            return;
                        }

                        Onyx::Modules::NodePath path;
                        Onyx::Modules::NodePath found;
                        bool matched = false;
                        for (uint32_t i = 0; i < doc->roots.size() && !matched; ++i) {
                            path.indices = {i};
                            matched = FindFirstSceneEntry(app.GetWorkspace(), doc->roots[i], path, found);
                        }
                        if (!matched) {
                            LOG_WARN("[MinimalViewer] --open-first-scene: no Scene-routed entry "
                                     "found in document %llu", (unsigned long long)ev.id);
                            return;
                        }

                        Onyx::App::ViewerOpener opener;
                        opener.openScene = [&app, proofPanelPtr](Onyx::Modules::DocumentId doc, std::string name,
                                                                 std::unique_ptr<Onyx::Parsers::SceneData> scene) {
                            auto viewport = std::make_shared<Onyx::Viewers::Viewport3D>(name);
                            viewport->LoadScene(std::move(scene));
                            app.getDocumentWindow().AddTab(viewport, doc);
                            proofPanelPtr->SetTarget(viewport);
                            LOG_INFO("[MinimalViewer] --open-first-scene: opened Viewport3D '%s'",
                                     name.c_str());
                        };

                        Onyx::App::OpenSelection(app.GetWorkspace(),
                                                 Onyx::Modules::SelectionChanged{doc->id, found}, opener);
                    });
        }

        // The legacy hex tab is a FALLBACK, not a companion: it used to be
        // queued unconditionally, so opening a file a module DOES understand
        // (e.g. cube.obx) showed a raw hex dump tab fighting the real
        // viewer for attention -- exactly what confused a hands-on tester of
        // the Vulkan viewport. Now it only opens when nothing claimed the
        // file: either OpenAsync found no module willing to accept it at all
        // (returns 0 -- "only when no module accepted the file", see
        // Workspace::OpenAsync's own doc comment), or a module did claim it
        // but the parse came back with an empty tree (TreeReady{ok=false},
        // or ok=true with zero roots).
        auto openHexTab = [&app](const std::string& p) {
            Onyx::Vfs::OsFile file(p);
            if (!file.IsValid()) return;
            std::vector<uint8_t> bytes = file.ReadAll();
            constexpr size_t kMaxHexBytes = 64 * 1024; // demo hex view: cap to avoid OOM on large files
            if (bytes.size() > kMaxHexBytes) bytes.resize(kMaxHexBytes);
            auto viewer = std::make_shared<MinimalViewer::HexViewer>(p, std::move(bytes));
            app.getDocumentWindow().AddTab(viewer);
        };

        if (openedId == 0) {
            // No module even claimed the file -- nothing to wait for.
            LOG_INFO("[MinimalViewer] no module claimed '%s'; opening the hex tab as a fallback",
                     path.c_str());
            openHexTab(path);
        } else {
            hexFallbackSub = app.GetWorkspace().Events().On<Onyx::Modules::TreeReady>(
                [&app, openedId, path, openHexTab](const Onyx::Modules::TreeReady& ev) {
                    if (ev.id != openedId) return;

                    const Onyx::Modules::Document* doc = nullptr;
                    for (const auto& d : app.GetWorkspace().Documents()) {
                        if (d->id == ev.id) { doc = d.get(); break; }
                    }
                    bool claimed = ev.ok && doc && !doc->roots.empty();
                    if (claimed) {
                        LOG_INFO("[MinimalViewer] a module claimed '%s'; skipping the hex fallback "
                                 "tab", path.c_str());
                        return;
                    }
                    LOG_INFO("[MinimalViewer] '%s' produced no content; opening the hex tab as a "
                             "fallback", path.c_str());
                    openHexTab(path);
                });
        }
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
    // `MinimalViewer --open-first-scene [file]` (T14): same proof, for the
    // Scene viewer (Viewport3D) instead -- closes the gap T10 left: nothing
    // exercised Viewport3D's Vulkan draw path headlessly because no module
    // registered with MinimalViewer had a Scene decoder until OnyxBox
    // gained one (Examples/OnyxBox/OnyxBoxModule.cpp's kind=3 mesh entry).
    bool uiTest = false;
    bool openFirstImage = false;
    bool openFirstScene = false;
    const char* optionalPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ui-test") == 0)
            uiTest = true;
        else if (std::strcmp(argv[i], "--open-first-image") == 0)
            openFirstImage = true;
        else if (std::strcmp(argv[i], "--open-first-scene") == 0)
            openFirstScene = true;
        else if (!optionalPath)
            optionalPath = argv[i];
    }
    return RunGui(optionalPath, uiTest, openFirstImage, openFirstScene);
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return main(__argc, __argv);
}
#endif
