#include "ShellGui.h"

#include <filesystem>
#include <utility>

#include "Onyx/Core/Services/Log.h"
#include "Onyx/Shell/Dock.h"
#include "Onyx/Shell/MenuBar.h"
#include "Onyx/Ui/AssetPaths.h"
#include "Onyx/Ui/Fonts.h"

namespace Onyx::Shell {

namespace {

using Out = Result<std::unique_ptr<GuiStack>>;

// Builds the atlas from the bundled fonts when they are installed; otherwise keeps ImGui's default font.
void BuildAtlas(Ui::Theme& theme) {
    ImGuiIO& io = ImGui::GetIO();
    const std::filesystem::path text = Ui::FindAsset("fonts/SFMono-Regular.otf");
    const std::filesystem::path icons = Ui::FindAsset("fonts/SFSymbols.ttf");
    const Ui::ThemeResolved& applied = theme.Applied();
    if (!text.empty() && !icons.empty() && applied.atlasRefPx > 0.0f) {
        auto fonts = Ui::BuildFonts(*io.Fonts, text, applied.atlasRefPx, icons);
        if (fonts.ok()) {
            io.FontDefault = fonts.value().text;
            return;
        }
        Services::Log::Default().Write(Services::LogLevel::Warn, "shell.fonts", "{}: {}", fonts.error().code,
                                       fonts.error().message);
    }
    io.Fonts->AddFontDefault();
}

}

Result<std::unique_ptr<GuiStack>> GuiStack::Create(const ShellOptions& options, Ui::Theme& theme,
                                                   Ui::FrameClock& clock) {
    auto gui = std::make_unique<GuiStack>();

    WindowOptions windowOptions;
    windowOptions.title = options.appName;
    windowOptions.size = options.windowSize;
    windowOptions.borderless = options.borderless;
    windowOptions.visible = !options.hiddenWindow;
    auto window = NativeWindow::Create(windowOptions);
    if (!window.ok()) {
        return Out::Fail(window.error());
    }
    gui->window = std::move(window.value());

    Gpu::DeviceOptions deviceOptions;
    deviceOptions.validation = options.gpuValidation;
    deviceOptions.presentSupport = true;
    deviceOptions.instanceExtensions = gui->window->RequiredInstanceExtensions();
    auto device = Gpu::Device::Create(deviceOptions);
    if (!device.ok()) {
        return Out::Fail(device.error());
    }
    gui->device = std::move(device.value());

    auto surface = gui->window->CreateSurface(gui->device->Instance());
    if (!surface.ok()) {
        return Out::Fail(surface.error());
    }
    gui->surface = surface.value();

    auto presenter = Presenter::Create(*gui->device, gui->surface, gui->window->FramebufferSize());
    if (!presenter.ok()) {
        return Out::Fail(presenter.error());
    }
    gui->presenter = std::move(presenter.value());

    auto frames = Gpu::FrameRing::Create(*gui->device);
    if (!frames.ok()) {
        return Out::Fail(frames.error());
    }
    gui->frames = std::move(frames.value());

    IMGUI_CHECKVERSION();
    gui->imgui = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    gui->iniPath = (options.configDir / "imgui.ini").string();
    gui->freshLayout = !std::filesystem::exists(options.configDir / "imgui.ini");
    io.IniFilename = gui->iniPath.c_str();

    auto platform = GlfwPlatform::Create(*gui->window);
    if (!platform.ok()) {
        return Out::Fail(platform.error());
    }
    gui->platform = std::move(platform.value());

    Ui::BackendOptions backendOptions;
    backendOptions.colorFormat = gui->presenter->Format();
    backendOptions.imageCount = gui->presenter->ImageCount();
    auto backend = Ui::ImGuiVulkanBackend::Create(*gui->device, backendOptions);
    if (!backend.ok()) {
        return Out::Fail(backend.error());
    }
    gui->backend = std::move(backend.value());

    auto upload = Gpu::UploadContext::Create(*gui->device);
    if (!upload.ok()) {
        return Out::Fail(upload.error());
    }
    gui->upload = std::move(upload.value());
    gui->textures = std::make_unique<Ui::TexturePool>(*gui->device, *gui->upload, *gui->backend);

    Ui::ThemeEnvironment environment = theme.Env();
    environment.nativeScale = gui->window->ContentScale();
    environment.defaultFontPath = Ui::FindAsset("fonts/SFMono-Regular.otf").string();
    theme.SetEnvironment(environment);
    theme.Commit();
    BuildAtlas(theme);

    gui->frame = std::make_unique<ShellFrame>(*gui->window, *gui->device, *gui->presenter, *gui->frames,
                                              *gui->backend, theme, clock);
    return Out::Ok(std::move(gui));
}

void GuiStack::WaitIdle() {
    if (device) {
        auto idle = device->GraphicsQueue().WaitIdle();
        if (!idle.ok()) {
            Services::Log::Default().Write(Services::LogLevel::Warn, "shell.gpu", "{}: {}", idle.error().code,
                                           idle.error().message);
        }
    }
}

GuiStack::~GuiStack() {
    WaitIdle();
    frame.reset();
    textures.reset();
    upload.reset();
    if (imgui) {
        ImGui::SetCurrentContext(imgui);
    }
    backend.reset();
    platform.reset();
    if (imgui) {
        ImGui::DestroyContext(imgui);
    }
    frames.reset();
    presenter.reset();
    if (surface != VK_NULL_HANDLE && device) {
        device->InstanceFn().destroySurfaceKHR(device->Instance(), surface, nullptr);
    }
    device.reset();
    window.reset();
}

void Shell::DrawFrame(Gpu::Frame& frame) {
    Ui::UiContext ui = Context(&frame);
    ui.dt = ImGui::GetIO().DeltaTime;

    m_gui->window->SetTitleBarRect(DrawMenuBar(ui));
    m_commands.PollShortcuts(ui);
    const ImGuiID dockspace = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    if (m_gui->freshLayout || m_resetLayout) {
        ApplyDockLayout(dockspace, m_layout, m_panels);
        m_gui->freshLayout = false;
        m_resetLayout = false;
    }
    m_panels.DrawAll(ui);

    const Gpu::FenceValue uploaded = m_gui->textures->Flush();
    frame.AddWait(uploaded);
    if (const auto& fault = m_gui->textures->Fault()) {
        Services::Log::Default().Write(Services::LogLevel::Error, "shell.textures", "{}: {}", fault->code,
                                       fault->message);
    }
}

}
