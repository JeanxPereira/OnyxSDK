#include "SelfTest.h"
#include "HexViewer.h"

#include <Onyx/App/Window.h>
#include <Onyx/App/App.h>
#include <Onyx/Services/Threading.h>
#include <Onyx/Vfs/OsFile.h>
#include <Onyx/Viewers/DocumentWindow.h>

#include <cstring>
#include <memory>
#include <vector>
#include <cstdint>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

static int RunGui(const char* optionalPath, bool uiTest) {
    Onyx::Threading::MarkMainThread();

    Onyx::App::Window::initNative();
    Onyx::App::Window window;

    // If a file was passed, queue a hex tab once the App is initialised. The
    // registrar runs inside App::init(), after the engine's generic panels.
    std::string path = optionalPath ? optionalPath : "";
    window.app().SetRegistrar([path, uiTest](Onyx::App::App& app) {
        // UI test mode: open the engine's widget/theme/icon gallery straight
        // away so the UI can be polished without loading any asset.
        if (uiTest) app.setPanelVisible("UI Gallery", true);
        if (path.empty()) return;
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
    bool uiTest = false;
    const char* optionalPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ui-test") == 0)
            uiTest = true;
        else if (!optionalPath)
            optionalPath = argv[i];
    }
    return RunGui(optionalPath, uiTest);
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return main(__argc, __argv);
}
#endif
