#include "Onyx/Shell/Shell.h"

#include <imgui.h>

#include <iostream>
#include <system_error>

#include "Onyx/Cli/StockVerbs.h"
#include "Onyx/Core/Services/Log.h"
#include "Onyx/Core/Services/Paths.h"
#include "Onyx/Shell/StockCommands.h"
#include "Onyx/Ui/Panels/DocumentBrowser.h"
#include "Onyx/Ui/Panels/Inspector.h"
#include "Onyx/Ui/Panels/LogPanel.h"
#include "Onyx/Ui/Panels/Problems.h"
#include "Onyx/Ui/Panels/SettingsPanel.h"
#include "Onyx/Ui/Panels/Timeline.h"
#include "Onyx/Ui/Panels/Transport.h"
#include "Onyx/Ui/Panels/Viewport.h"
#include "Onyx/Ui/Viewers/DocumentTabs.h"
#include "Onyx/Ui/Viewers/Opening.h"
#include "Onyx/Ui/Viewers/SceneView.h"
#include "Onyx/Ui/Viewers/StockViewers.h"
#include "ShellGui.h"

namespace Onyx::Shell {

namespace {

constexpr auto kSaveInterval = std::chrono::seconds(5);
constexpr double kHiddenIdleSeconds = 0.005;
constexpr double kIdleSeconds = 0.25;
constexpr double kBusyPollSeconds = 0.1;
constexpr double kOpenPollSeconds = 1.0;
constexpr const char* kTitleSeparator = " \xE2\x80\x94 ";

ShellOptions ResolveOptions(ShellOptions options) {
    if (options.appName.empty()) {
        options.appName = "Onyx";
    }
    if (options.configDir.empty()) {
        options.configDir = Services::Paths::UserConfig(options.appName);
    }
    std::error_code ec;
    std::filesystem::create_directories(options.configDir, ec);
    if (ec) {
        Services::Log::Default().Write(Services::LogLevel::Warn, "shell.config", "cannot create {}: {}",
                                       Services::Paths::ToUtf8(options.configDir), ec.message());
    }
    return options;
}

Modules::WorkspaceOptions WorkspaceFor(const ShellOptions& options) {
    Modules::WorkspaceOptions workspace;
    workspace.workerThreads = options.workerThreads;
    workspace.settingsPath = options.configDir / "settings.toml";
    return workspace;
}

void InstallStockPanels(Shell& shell) {
    Ui::RegisterStockViewers(shell.Viewers());
    Ui::RegisterSceneViewer(shell.Viewers());

    auto browser = std::make_unique<Ui::DocumentBrowser>();
    browser->onOpen = [&shell](Ui::UiContext& ui, const Ui::Selection& selection) {
        ui.clock.RequestAnimation(kOpenPollSeconds);
        auto placed = Ui::OpenEntry(ui, selection, [&shell](std::unique_ptr<Ui::IDocumentView> view) {
            if (auto* tabs = dynamic_cast<Ui::DocumentTabs*>(shell.Panels().Find("onyx.documents"))) {
                tabs->Add(std::move(view));
            }
        });
        if (!placed.ok()) {
            Services::Log::Default().Write(Services::LogLevel::Warn, "shell.open", "{}: {}", placed.error().code,
                                           placed.error().message);
        }
    };
    shell.Panels().Add(std::move(browser));
    shell.Panels().Add(std::make_unique<Ui::DocumentTabs>(shell.Workspace()));
    shell.Panels().Add(std::make_unique<Ui::Inspector>());
    shell.Panels().Add(std::make_unique<Ui::Problems>());
    shell.Panels().Add(std::make_unique<Ui::LogPanel>());
    auto settings = std::make_unique<Ui::SettingsPanel>();
    settings->visible = false;
    shell.Panels().Add(std::move(settings));
    shell.Panels().Add(std::make_unique<Ui::Viewport>());
    shell.Panels().Add(std::make_unique<Ui::Transport>());
    shell.Panels().Add(std::make_unique<Ui::Timeline>());
}

}

Shell::Shell(ShellOptions options)
    : m_options(ResolveOptions(std::move(options))),
      m_workspace(WorkspaceFor(m_options)),
      m_theme(m_workspace.Settings()),
      m_recents(m_workspace.Settings()),
      m_dialogs(NativeDialogs()),
      m_layout(DefaultLayout()),
      m_savedTheme(m_theme.State()) {
    m_verbs.Add(Cli::StockVerbs());
    InstallStockPanels(*this);
    AddStockCommands(m_commands, *this);
}

Shell::~Shell() {
    Teardown();
    SaveSettings();
}

void Shell::Teardown() {
    ApplyPendingAudio();
    if (m_audio) {
        m_audio->Stop();
    }
    if (m_gui) {
        m_gui->WaitIdle();
        ImGui::SetCurrentContext(m_gui->imgui);
    }
    std::vector<std::string> ids;
    for (const auto& panel : m_panels.All()) {
        ids.emplace_back(panel->Id());
    }
    for (const std::string& id : ids) {
        m_panels.Remove(id);
    }
    m_gui.reset();
}

Gpu::Device* Shell::GpuDevice() {
    return m_gui ? m_gui->device.get() : nullptr;
}

Ui::UiContext Shell::Context(Gpu::Frame* frame) {
    Gpu::Device* device = m_gui ? m_gui->device.get() : nullptr;
    Ui::TexturePool* textures = m_gui ? m_gui->textures.get() : nullptr;
    const Ui::FontSet* fonts = m_gui && m_gui->fonts.text ? &m_gui->fonts : nullptr;
    return Ui::UiContext{m_workspace, device, frame, textures, m_theme, m_commands, m_viewers, m_panels, m_clock,
                         0.0f,        m_audio.get(), fonts};
}

void Shell::AddExtension(std::unique_ptr<IShellExtension> extension) {
    if (!extension) {
        return;
    }
    extension->Install(*this);
    m_extensions.push_back(std::move(extension));
    AddStockCommands(m_commands, *this);
}

void Shell::SetLifecycle(std::unique_ptr<LifecycleDelegate> delegate) {
    m_lifecycle = std::move(delegate);
}

void Shell::SetAudioOutput(std::unique_ptr<Ui::IAudioOutput> output) {
    if (m_inFrame) {
        m_pendingAudio = std::move(output);
        return;
    }
    m_pendingAudio.reset();
    ReplaceAudio(std::move(output));
}

void Shell::ReplaceAudio(std::unique_ptr<Ui::IAudioOutput> output) {
    if (m_audio) {
        m_audio->Stop();
    }
    m_audio = std::move(output);
}

void Shell::ApplyPendingAudio() {
    if (!m_pendingAudio) {
        return;
    }
    std::unique_ptr<Ui::IAudioOutput> output = std::move(*m_pendingAudio);
    m_pendingAudio.reset();
    ReplaceAudio(std::move(output));
}

void Shell::SetDialogs(std::unique_ptr<IDialogs> dialogs) {
    m_dialogs = dialogs ? std::move(dialogs) : NativeDialogs();
}

Result<void> Shell::Open(const std::filesystem::path& path, std::string_view moduleHint) {
    const Modules::DocumentId id = m_workspace.OpenAsync(path, moduleHint);
    if (id == 0) {
        return Result<void>::Fail({"shell.open", "no module opens " + Services::Paths::ToUtf8(path), 0});
    }
    const Modules::Document* doc = m_workspace.Get(id);
    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    m_recents.Add(ec ? path : absolute, doc && doc->module ? doc->module->Info().id : std::string());
    AddStockCommands(m_commands, *this);
    m_clock.RequestAnimation(kOpenPollSeconds);
    return Result<void>::Ok();
}

void Shell::RequestClose() {
    m_closeRequested = true;
    if (m_gui) {
        m_gui->window->SetShouldClose(true);
    }
}

bool Shell::ConfirmClose() {
    return !m_lifecycle || m_lifecycle->WillClose(*this);
}

void Shell::SyncTheme() {
    if (m_theme.State() == m_savedTheme) {
        return;
    }
    // Apply in the settings panel writes the state itself; a second write would dirty the file again.
    if (Ui::Theme::ReadState(Settings()) != m_theme.State()) {
        m_theme.Save();
    }
    m_savedTheme = m_theme.State();
}

void Shell::SaveSettings() {
    SyncTheme();
    auto saved = Settings().SaveIfDirty();
    if (!saved.ok()) {
        Services::Log::Default().Write(Services::LogLevel::Error, "shell.settings", "{}: {}", saved.error().code,
                                       saved.error().message);
    }
}

void Shell::UpdateTitle() {
    std::string title = m_options.appName;
    const Modules::Document* shown = nullptr;
    if (auto* tabs = dynamic_cast<Ui::DocumentTabs*>(m_panels.Find("onyx.documents"))) {
        if (const Ui::IDocumentView* active = tabs->Active()) {
            shown = m_workspace.Get(active->Document());
        }
    }
    const auto documents = m_workspace.Documents();
    if (!shown && !documents.empty()) {
        shown = documents.back().get();
    }
    if (shown) {
        title = Services::Paths::ToUtf8(shown->path.filename()) + kTitleSeparator + m_options.appName;
    }
    if (title != m_title) {
        m_title = std::move(title);
        m_gui->window->SetTitle(m_title);
    }
}

void Shell::ServiceDialogs() {
    if (!m_openDialogPending) {
        return;
    }
    m_openDialogPending = false;
    std::vector<Modules::OpenFilter> filters;
    for (const Modules::IGameModule* module : m_workspace.Modules()) {
        const auto& own = module->Info().openFilters;
        filters.insert(filters.end(), own.begin(), own.end());
    }
    const std::optional<std::filesystem::path> chosen = m_dialogs->OpenFile(filters);
    if (!chosen) {
        return;
    }
    auto opened = Open(*chosen);
    if (!opened.ok()) {
        Services::Log::Default().Write(Services::LogLevel::Warn, "shell.dialog", "{}: {}", opened.error().code,
                                       opened.error().message);
    }
}

Result<bool> Shell::Step() {
    using Out = Result<bool>;
    if (!m_gui) {
        auto gui = GuiStack::Create(m_options, m_theme, m_clock);
        if (!gui.ok()) {
            return Out::Fail(gui.error());
        }
        m_gui = std::move(gui.value());
        m_gui->window->SetDropHandler([this](std::vector<std::filesystem::path> paths) {
            for (const auto& path : paths) {
                auto opened = Open(path);
                if (!opened.ok()) {
                    Services::Log::Default().Write(Services::LogLevel::Warn, "shell.open", "{}: {}",
                                                   opened.error().code, opened.error().message);
                }
            }
            if (m_lifecycle) {
                m_lifecycle->FilesDropped(*this, paths);
            }
        });
        if (m_closeRequested) {
            m_gui->window->SetShouldClose(true);
        }
    }
    ImGui::SetCurrentContext(m_gui->imgui);

    m_workspace.Pump();
    for (const auto& doc : m_workspace.Documents()) {
        if (!doc->ready.load()) {
            m_clock.RequestAnimation(kBusyPollSeconds);
            break;
        }
    }
    UpdateTitle();
    m_gui->frame->SetIdleWaitCap(m_options.hiddenWindow ? kHiddenIdleSeconds : kIdleSeconds);

    auto stepped = m_gui->frame->Step([this](Gpu::Frame& frame) { DrawFrame(frame); });
    if (!stepped.ok()) {
        return stepped;
    }
    bool open = stepped.value();
    if (!open && !ConfirmClose()) {
        m_gui->window->SetShouldClose(false);
        m_closeRequested = false;
        open = true;
    }
    if (!open) {
        SaveSettings();
        return Out::Ok(false);
    }

    ServiceDialogs();
    if (!m_started) {
        m_started = true;
        if (m_lifecycle) {
            m_lifecycle->DidStart(*this);
        }
    }
    SyncTheme();
    if (Settings().Dirty()) {
        const auto now = std::chrono::steady_clock::now();
        if (!m_dirtySince) {
            m_dirtySince = now;
        } else if (now - *m_dirtySince >= kSaveInterval) {
            SaveSettings();
            m_dirtySince.reset();
        }
    } else {
        m_dirtySince.reset();
    }
    return Out::Ok(true);
}

int Shell::Run(int argc, char** argv) {
    const ArgsRoute route = RouteArgs(argc, argv, m_verbs);
    if (route.kind == ArgsRoute::Kind::Cli) {
        return RunCli(*this, argc, argv);
    }
    if (route.kind == ArgsRoute::Kind::GuiOpen) {
        auto opened = Open(route.file);
        if (!opened.ok()) {
            Services::Log::Default().Write(Services::LogLevel::Warn, "shell.open", "{}: {}", opened.error().code,
                                           opened.error().message);
        }
    }
    for (;;) {
        auto stepped = Step();
        if (!stepped.ok()) {
            Services::Log::Default().Write(Services::LogLevel::Error, "shell.frame", "{}: {}", stepped.error().code,
                                           stepped.error().message);
            std::cerr << stepped.error().code << ": " << stepped.error().message << "\n";
            Teardown();
            return 1;
        }
        if (!stepped.value()) {
            Teardown();
            return 0;
        }
    }
}

}
