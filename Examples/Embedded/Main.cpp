// A host that owns its window, its Vulkan device and its ImGui context, and lends Onyx a second queue
// of the graphics family. Onyx contributes panels and viewers; the host draws one window of its own
// beside them and presents through its own swapchain.
#include <OnyxBox/Module.h>
#include <OnyxBox/Writer.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_internal.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "HostWindow.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include "Onyx/Core/Modules/Workspace.h"
#include "Onyx/Gpu/Device.h"
#include "Onyx/Gpu/FrameRing.h"
#include "Onyx/Gpu/Upload.h"
#include "Onyx/Ui/AssetPaths.h"
#include "Onyx/Ui/Command.h"
#include "Onyx/Ui/Fonts.h"
#include "Onyx/Ui/FrameClock.h"
#include "Onyx/Ui/ImGuiVulkanBackend.h"
#include "Onyx/Ui/PanelHost.h"
#include "Onyx/Ui/PathText.h"
#include "Onyx/Ui/Panels/DocumentBrowser.h"
#include "Onyx/Ui/Panels/Viewport.h"
#include "Onyx/Ui/TexturePool.h"
#include "Onyx/Ui/Theme.h"
#include "Onyx/Ui/UiContext.h"
#include "Onyx/Ui/ViewerRegistry.h"
#include "Onyx/Ui/Viewers/DocumentTabs.h"
#include "Onyx/Ui/Viewers/Opening.h"
#include "Onyx/Ui/Viewers/SceneView.h"
#include "Onyx/Ui/Viewers/StockViewers.h"

namespace {

using namespace Onyx;

constexpr int kExitNoDevice = 77;
constexpr int kExitUsage = 2;
// With --frames, the most frames the host draws while waiting for the sample.
constexpr long kMaxFrames = 600;

struct Options {
    long frames = -1;
    std::filesystem::path open;
};

void Require(const Result<void>& result) {
    if (!result.ok()) {
        throw std::runtime_error(result.error().code + ": " + result.error().message);
    }
}

template <class T>
    requires(!std::is_void_v<T>)
T Require(Result<T>&& result) {
    if (!result.ok()) {
        throw std::runtime_error(result.error().code + ": " + result.error().message);
    }
    return std::move(result.value());
}

// The host's ImGui context with the GLFW platform backend. Declared before the renderer backend so the
// renderer is destroyed while the context is still alive.
struct ImGuiScope {
    explicit ImGuiScope(GLFWwindow* window) {
        IMGUI_CHECKVERSION();
        context = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.IniFilename = nullptr;
        if (!ImGui_ImplGlfw_InitForVulkan(window, true)) {
            ImGui::DestroyContext(context);
            throw std::runtime_error("ImGui_ImplGlfw_InitForVulkan failed");
        }
    }
    ~ImGuiScope() {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext(context);
    }
    ImGuiScope(const ImGuiScope&) = delete;
    ImGuiScope& operator=(const ImGuiScope&) = delete;

    ImGuiContext* context = nullptr;
};

// The host's side of the theme contract: rebuild the atlas the theme asks for and make its text font the
// default. On failure the atlas holds ImGui's default font.
void BuildAtlas(const Ui::Theme& theme, Ui::FontSet& fonts) {
    ImGuiIO& io = ImGui::GetIO();
    auto built = Ui::BuildThemeFonts(theme, *io.Fonts);
    if (!built.ok()) {
        std::fprintf(stderr, "fonts: %s\n", built.error().message.c_str());
    }
    fonts = built.ok() ? built.value() : Ui::FontSet{};
    io.FontDefault = fonts.text;
}

std::filesystem::path WriteSample() {
    std::vector<std::byte> pixels(64 * 64 * 4);
    for (size_t y = 0; y < 64; ++y) {
        for (size_t x = 0; x < 64; ++x) {
            std::byte* texel = &pixels[(y * 64 + x) * 4];
            texel[0] = static_cast<std::byte>(x * 4);
            texel[1] = static_cast<std::byte>(y * 4);
            texel[2] = static_cast<std::byte>(((x / 8 + y / 8) % 2) ? 200 : 60);
            texel[3] = static_cast<std::byte>(255);
        }
    }
    OnyxBox::Writer writer;
    writer.AddImage("img", 64, 64, pixels);
    writer.AddText("txt", "Written by the host.\nOnyx only reads it.");
    writer.AddMesh("mesh", {0.95f, 0.55f, 0.20f, 1.0f}, {0, 0, 0}, {1.0f, 0.6f, 0.8f});
    const std::vector<std::byte> bytes = writer.Build();

    const std::filesystem::path path = std::filesystem::temp_directory_path() / "EmbeddedSample.obx";
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        throw std::runtime_error("cannot write " + path.string());
    }
    return path;
}

std::string WindowName(Ui::PanelHost& panels, std::string_view id) {
    const Ui::IPanel* panel = panels.Find(id);
    return panel ? std::string(panel->Title()) + "##" + std::string(panel->Id()) : std::string();
}

void BuildLayout(ImGuiID root, Ui::PanelHost& panels) {
    ImGui::DockBuilderRemoveNode(root);
    ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(root, ImGui::GetMainViewport()->WorkSize);
    ImGuiID left = 0;
    ImGuiID centre = 0;
    ImGui::DockBuilderSplitNode(root, ImGuiDir_Left, 0.25f, &left, &centre);
    ImGuiID host = 0;
    ImGuiID browser = 0;
    ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.35f, &host, &browser);
    ImGuiID viewport = 0;
    ImGuiID viewer = 0;
    ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.5f, &viewport, &viewer);
    ImGui::DockBuilderDockWindow("Host", host);
    ImGui::DockBuilderDockWindow(WindowName(panels, "onyx.browser").c_str(), browser);
    ImGui::DockBuilderDockWindow(WindowName(panels, "onyx.documents").c_str(), viewer);
    ImGui::DockBuilderDockWindow(WindowName(panels, "onyx.viewport").c_str(), viewport);
    ImGui::DockBuilderFinish(root);
}

int RunHost(const Options& options) {
    // Destruction runs bottom to top: panels before the texture pool they registered in, the pool and
    // the renderer before Onyx's device, Onyx's device before the host's own.
    Embedded::HostWindow host("Embedded", 1280, 800);

    Gpu::ExternalDevice external;
    external.instance = host.Instance();
    external.physicalDevice = host.PhysicalDevice();
    external.device = host.Device();
    external.queue = host.OnyxQueue();
    external.queueFamily = host.QueueFamily();
    external.getInstanceProcAddr = vkGetInstanceProcAddr;
    external.getDeviceProcAddr = vkGetDeviceProcAddr;
    external.enabled = {true, true, true};
    std::unique_ptr<Gpu::Device> device = Require(Gpu::Device::Adopt(external));
    std::unique_ptr<Gpu::FrameRing> frames = Require(Gpu::FrameRing::Create(*device));

    ImGuiScope imgui(host.Window());
    Ui::BackendOptions backendOptions;
    backendOptions.colorFormat = host.Format();
    backendOptions.imageCount = host.ImageCount();
    // The backend is the ITextureBinder: the pool hands it every image view a panel shows.
    std::unique_ptr<Ui::ImGuiVulkanBackend> backend = Require(Ui::ImGuiVulkanBackend::Create(*device, backendOptions));
    std::unique_ptr<Gpu::UploadContext> upload = Require(Gpu::UploadContext::Create(*device));
    Ui::TexturePool pool(*device, *upload, *backend);

    Modules::Workspace workspace;
    Require(workspace.AddModule(std::make_unique<OnyxBox::Module>()));
    Ui::Theme theme(workspace.Settings());
    Ui::FrameClock clock;
    Ui::CommandRegistry commands;
    Ui::ViewerRegistry viewers;
    Ui::RegisterStockViewers(viewers);
    Ui::RegisterSceneViewer(viewers);
    Ui::FontSet fonts;
    {
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        glfwGetWindowContentScale(host.Window(), &scaleX, &scaleY);
        Ui::ThemeEnvironment environment = theme.Env();
        environment.nativeScale = scaleX;
        environment.defaultFontPath = Ui::PathToUtf8(Ui::FindAsset("fonts/SFMono-Regular.otf"));
        theme.SetEnvironment(environment);
        theme.Commit();
        BuildAtlas(theme, fonts);
        theme.ClearAtlasDirty();
    }

    Ui::PanelHost panels;
    auto tabs = std::make_unique<Ui::DocumentTabs>(workspace);
    Ui::DocumentTabs* tabsView = tabs.get();
    auto browser = std::make_unique<Ui::DocumentBrowser>();
    browser->onOpen = [tabsView](Ui::UiContext& ui, const Ui::Selection& selection) {
        auto placed = Ui::OpenEntry(ui, selection, [tabsView](std::unique_ptr<Ui::IDocumentView> view) {
            tabsView->Add(std::move(view));
        });
        if (!placed.ok()) {
            std::fprintf(stderr, "open: %s\n", placed.error().message.c_str());
        }
    };
    panels.Add(std::move(browser));
    panels.Add(std::move(tabs));
    panels.Add(std::make_unique<Ui::Viewport>());

    const std::filesystem::path sample = options.open.empty() ? WriteSample() : options.open;
    Modules::DocumentId sampleId = workspace.OpenAsync(sample, options.open.empty() ? "obx" : "");
    if (sampleId == 0) {
        throw std::runtime_error("no module opens " + sample.string());
    }
    bool shown = !options.open.empty();
    bool freshLayout = true;
    long drawn = 0;
    // With --frames N: frames drawn since the sample became ready (parsed and, for the built-in sample,
    // both tabs placed), so the views have drawn and uploaded before the host stops.
    long drawnReady = 0;
    const auto sampleReady = [&] {
        const Modules::Document* document = workspace.Get(sampleId);
        return shown && document && document->ready.load() && (!options.open.empty() || tabsView->Count() == 2);
    };
    double last = glfwGetTime();

    // Declared after everything the GPU may still be using, so an exception out of the loop waits for the
    // queues before the panels, the pool and the device go.
    struct IdleOnExit {
        Embedded::HostWindow& host;
        ~IdleOnExit() {
            try {
                host.WaitIdle();
            } catch (const std::exception& failure) {
                std::fprintf(stderr, "Embedded: %s\n", failure.what());
            }
        }
    } idleOnExit{host};

    while (!glfwWindowShouldClose(host.Window()) && (options.frames < 0 || drawnReady < options.frames)) {
        if (options.frames >= 0 && drawn >= kMaxFrames) {
            throw std::runtime_error("the sample was not ready after " + std::to_string(kMaxFrames) + " frames");
        }
        glfwPollEvents();
        if (host.Minimised()) {
            glfwWaitEventsTimeout(0.05);
            continue;
        }
        // imgui's vertex buffer ring keeps the image count given at Create: it only has to cover the
        // frames in flight, which FrameRing bounds, so a recreated swapchain needs no backend change.
        host.Refresh();

        const double now = glfwGetTime();
        const float dt = static_cast<float>(now - last);
        last = now;
        clock.BeginFrame(now);
        theme.Update(dt);
        theme.Commit();
        if (theme.AtlasDirty()) {
            BuildAtlas(theme, fonts);
            backend->RebuildFonts();
            theme.ClearAtlasDirty();
            Require(backend->Fault());
        }
        workspace.Pump();

        Gpu::Frame& frame = *Require(frames->Begin());
        Embedded::SwapImage image;
        VkSemaphore acquired = VK_NULL_HANDLE;
        if (!host.Acquire(frame.Index(), image, acquired)) {
            Require(frames->End(frame));
            continue;
        }
        frame.AddWait(acquired, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
        frame.AddSignal(host.PresentSemaphore(image));

        backend->NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        Ui::UiContext ui{workspace, device.get(), &frame, &pool, theme,  commands,
                         viewers,   panels,       clock,  dt,    nullptr, fonts.text ? &fonts : nullptr};
        const ImGuiID dockspace = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
        if (freshLayout) {
            BuildLayout(dockspace, panels);
            freshLayout = false;
        }

        if (ImGui::Begin("Host")) {
            ImGui::Text("GPU: %s", host.DeviceName());
            ImGui::Text("Queue family %u, %s", host.QueueFamily(),
                        host.SharesQueue() ? "queue shared with Onyx" : "second queue for Onyx");
            ImGui::Text("Frame %ld, %.1f ms", drawn, static_cast<double>(dt) * 1000.0);
            ImGui::Text("Open documents: %zu", workspace.Documents().size());
            if (ImGui::Button("Open the sample again")) {
                workspace.OpenAsync(sample, options.open.empty() ? "obx" : "");
            }
        }
        ImGui::End();

        // The host drives Onyx too: once the sample has parsed it opens two of its entries in tabs. The
        // decode runs on the job queue and DocumentTabs::Add happens in a later Pump, not here.
        const Modules::Document* document = workspace.Get(sampleId);
        if (!shown && document && document->ready.load()) {
            shown = true;
            for (const char* name : {"img", "mesh"}) {
                if (auto path = Modules::FindByName(*document, name)) {
                    auto opened = Ui::OpenEntry(ui, Ui::Selection{sampleId, *path},
                                                [tabsView](std::unique_ptr<Ui::IDocumentView> view) {
                                                    tabsView->Add(std::move(view));
                                                });
                    Require(opened);
                }
            }
        }

        panels.DrawAll(ui);
        frame.AddWait(pool.Flush());
        if (pool.Fault()) {
            throw std::runtime_error(pool.Fault()->code + ": " + pool.Fault()->message);
        }
        ImGui::Render();

        // The ImGui pass goes into the same command buffer the panels recorded into (the Viewport's
        // scene pass), after them: one submit per host frame.
        host.BeginRendering(frame.Cmd(), image);
        backend->Record(frame.Cmd(), ImGui::GetDrawData());
        host.EndRendering(frame.Cmd(), image);
        Require(frames->End(frame));
        host.Present(image);
        // imgui parks a failed Vulkan call in the backend; drain it every frame.
        Require(backend->Fault());
        ++drawn;
        if (sampleReady()) {
            ++drawnReady;
        }
    }
    host.WaitIdle();
    std::printf("Embedded: %ld frames on %s\n", drawn, host.DeviceName());
    if (host.ValidationMessages() > 0) {
        std::fprintf(stderr, "Embedded: the validation layer reported %u messages\n", host.ValidationMessages());
        return 1;
    }
    if (!host.Validating()) {
        std::printf("Embedded: VK_LAYER_KHRONOS_validation is not installed; validation was not checked\n");
    }
    return 0;
}

}

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            char* end = nullptr;
            options.frames = std::strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0' || options.frames < 0) {
                std::fprintf(stderr, "usage: Embedded [--frames N] [--open file]\n");
                return kExitUsage;
            }
        } else if (std::strcmp(argv[i], "--open") == 0 && i + 1 < argc) {
            options.open = argv[++i];
        } else {
            std::fprintf(stderr, "usage: Embedded [--frames N] [--open file]\n");
            return kExitUsage;
        }
    }
    try {
        return RunHost(options);
    } catch (const Embedded::NoDevice& skipped) {
        std::printf("Embedded: %s, skipping\n", skipped.what());
        return kExitNoDevice;
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "Embedded: %s\n", failure.what());
        return 1;
    }
}
