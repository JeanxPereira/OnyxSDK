#pragma once

#include <chrono>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec2.hpp>

#include "Onyx/Core/Cli/Verb.h"
#include "Onyx/Core/Modules/Workspace.h"
#include "Onyx/Core/Result.h"
#include "Onyx/Core/Services/Settings.h"
#include "Onyx/Shell/Dialogs.h"
#include "Onyx/Shell/Dock.h"
#include "Onyx/Shell/Lifecycle.h"
#include "Onyx/Shell/Recents.h"
#include "Onyx/Ui/AudioOutput.h"
#include "Onyx/Ui/Command.h"
#include "Onyx/Ui/FrameClock.h"
#include "Onyx/Ui/PanelHost.h"
#include "Onyx/Ui/Theme.h"
#include "Onyx/Ui/UiContext.h"
#include "Onyx/Ui/ViewerRegistry.h"

// Error codes:
//   shell.args    the command line names no verb or a malformed one (reported on the error stream)
//   shell.open    Open found no module for the file
//   shell.dialog  log category of a native dialog that could not be shown
// shell.window, shell.surface, Gpu and Ui codes from creating the window stack pass through Step.
namespace Onyx::Gpu {
class Device;
}

namespace Onyx::Shell {

struct ShellOptions {
    std::string appName;
    // Empty: Paths::UserConfig(appName). Holds settings.toml and imgui.ini.
    std::filesystem::path configDir;
    glm::ivec2 windowSize{1280, 800};
    // A frameless window with the menu bar as its caption: the default on Windows. Elsewhere the
    // window is always decorated and this is ignored (see WindowOptions::borderless).
#if defined(_WIN32)
    bool borderless = true;
#else
    bool borderless = false;
#endif
    // An invisible window that never idles long between frames: for tests and automation.
    bool hiddenWindow = false;
    unsigned workerThreads = 2;
    // Creates the GPU device with the Vulkan validation layer.
    bool gpuValidation = false;
};

class Shell;
struct GuiStack;

// A host's way to add viewers, panels, commands and verbs, for example those of one game module.
struct IShellExtension {
    virtual ~IShellExtension() = default;
    virtual void Install(Shell& shell) = 0;
};

// The application: owns the workspace, the registries and, once the first frame runs, the window,
// device, presenter and ImGui context, and tears them down in dependency order. Nothing here is
// reachable without a reference to the Shell. Main thread only.
class Shell {
public:
    // Registers the stock viewers, panels and commands. No window or GPU resource exists yet.
    explicit Shell(ShellOptions options);
    ~Shell();

    Shell(const Shell&) = delete;
    Shell& operator=(const Shell&) = delete;

    Modules::Workspace& Workspace() { return m_workspace; }
    Ui::CommandRegistry& Commands() { return m_commands; }
    Ui::ViewerRegistry& Viewers() { return m_viewers; }
    Ui::PanelHost& Panels() { return m_panels; }
    Ui::Theme& Theme() { return m_theme; }
    Services::Settings& Settings() { return m_workspace.Settings(); }
    Cli::VerbTable& Verbs() { return m_verbs; }
    Recents& RecentFiles() { return m_recents; }
    IDialogs& Dialogs() { return *m_dialogs; }
    const ShellOptions& Options() const { return m_options; }
    // Null until the first Step created the window stack.
    Gpu::Device* GpuDevice();

    // Installs at once; the extension is kept alive as long as the Shell.
    void AddExtension(std::unique_ptr<IShellExtension> extension);
    void SetLifecycle(std::unique_ptr<LifecycleDelegate> delegate);
    void SetDialogs(std::unique_ptr<IDialogs> dialogs);
    // The output Context() hands to every audio view. The Shell owns it, stops it when the window
    // stack goes down (before any panel is destroyed), and stops the one it replaces. Outside a frame
    // the replacement is immediate; called during a frame (from a panel's Draw) it takes effect at the
    // next frame boundary, so the frame keeps the output it started with. Null removes the output.
    void SetAudioOutput(std::unique_ptr<Ui::IAudioOutput> output);
    // Applies on the next layout reset and, when the config directory holds no imgui.ini, at start.
    void SetDockLayout(DockLayout layout) { m_layout = std::move(layout); }

    // Routes the command line through RouteArgs. Cli: the command line through Verbs() and its exit
    // code. Gui: the window loop, 0 after a normal close and 1 when a frame fails (the error is logged
    // and written to stderr). GuiOpen: the same loop after Open on the file; an Open failure is logged
    // and the window still opens. Run tears the window stack down, panels included, before it returns,
    // so a second Run on the same Shell draws an empty window.
    int Run(int argc, char** argv);

    // Starts the parse on the job queue, lists the file in Recents and names it in the title. Fails
    // with shell.open when no module claims the file. `moduleHint` is a module id or hint.
    Result<void> Open(const std::filesystem::path& path, std::string_view moduleHint = {});

    // One frame: pumps jobs and events, draws the UI, presents, then services requested dialogs and
    // the settings timer. Creates the window stack on the first call. False once the window closed
    // and the delegate agreed.
    Result<bool> Step();

    void RequestClose();
    void RequestOpenDialog() { m_openDialogPending = true; }
    bool OpenDialogPending() const { return m_openDialogPending; }
    void RequestResetLayout() { m_resetLayout = true; }

    // A context over the shell's registries; `frame` is null outside a frame.
    Ui::UiContext Context(Gpu::Frame* frame = nullptr);

private:
    void Teardown();
    void ServiceDialogs();
    // Writes the theme into the settings when it changed since the last write, so a live change
    // (the Appearance tab, a Mutate) reaches the file without Apply.
    void SyncTheme();
    void SaveSettings();
    bool ConfirmClose();
    void UpdateTitle();
    void DrawFrame(Gpu::Frame& frame);
    // Stops the current output and replaces it with `output`.
    void ReplaceAudio(std::unique_ptr<Ui::IAudioOutput> output);
    void ApplyPendingAudio();

    ShellOptions m_options;
    Modules::Workspace m_workspace;
    Ui::Theme m_theme;
    Ui::FrameClock m_clock;
    Ui::CommandRegistry m_commands;
    Ui::ViewerRegistry m_viewers;
    Ui::PanelHost m_panels;
    Cli::VerbTable m_verbs;
    Recents m_recents;
    std::unique_ptr<IDialogs> m_dialogs;
    std::unique_ptr<LifecycleDelegate> m_lifecycle;
    std::unique_ptr<Ui::IAudioOutput> m_audio;
    // A SetAudioOutput made during a frame, applied at the next frame boundary.
    std::optional<std::unique_ptr<Ui::IAudioOutput>> m_pendingAudio;
    bool m_inFrame = false;
    std::vector<std::unique_ptr<IShellExtension>> m_extensions;
    DockLayout m_layout;
    std::string m_title;
    bool m_openDialogPending = false;
    bool m_resetLayout = false;
    bool m_closeRequested = false;
    bool m_started = false;
    std::optional<std::chrono::steady_clock::time_point> m_dirtySince;
    Ui::ThemeState m_savedTheme;
    std::unique_ptr<GuiStack> m_gui;
};

// Runs argv[1..] through the shell's verbs (the stock ones plus anything added to Verbs()). Returns the
// Cli exit code; a usage failure also writes a shell.args line to `err`. Arguments are UTF-8: on Windows,
// when argv is the process's own, they are read again from the wide command line.
int RunCli(Shell& shell, int argc, char** argv, std::ostream& out, std::ostream& err);
// The same over std::cout and std::cerr. On Windows, when standard output is a console, its output code
// page is UTF-8 for the duration of the call. Cli::Run does not touch the console: a host that calls it
// directly sets the code page itself.
int RunCli(Shell& shell, int argc, char** argv);

// How Shell::Run treats a command line. No argument: Gui. Exactly one argument that is not a verb name
// and names an existing regular file: GuiOpen with that file. Anything else: Cli. Arguments are UTF-8
// as in RunCli.
struct ArgsRoute {
    enum class Kind { Gui, GuiOpen, Cli };
    Kind kind = Kind::Gui;
    std::filesystem::path file;
};
ArgsRoute RouteArgs(int argc, char** argv, const Cli::VerbTable& verbs);

}
