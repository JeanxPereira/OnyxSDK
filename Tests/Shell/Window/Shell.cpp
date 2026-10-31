#include <doctest/doctest.h>

#include <OnyxBox/Module.h>

#include <imgui.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "../Support/ShellSupport.h"
#include "Onyx/Shell/Shell.h"
#include "Onyx/Ui/Panels/DocumentBrowser.h"
#include "Onyx/Ui/Theme.h"
#include "Onyx/Ui/Viewers/DocumentTabs.h"
#include "Support/WindowFixture.h"

using namespace Onyx;
using Onyx::TestSupport::TempDir;
using Onyx::TestSupport::TestOptions;
using Onyx::TestSupport::WriteObx;

namespace {

// Steps until `done` holds or the step budget runs out; every step must succeed and keep the shell open.
bool StepUntil(Shell::Shell& shell, const std::function<bool()>& done, int maxSteps = 600) {
    for (int i = 0; i < maxSteps; ++i) {
        auto stepped = shell.Step();
        REQUIRE_MESSAGE(stepped.ok(), stepped.error().code << ": " << stepped.error().message);
        REQUIRE(stepped.value());
        if (done()) return true;
    }
    return false;
}

struct FixedDialogs final : Shell::IDialogs {
    std::filesystem::path answer;
    int asked = 0;
    std::optional<std::filesystem::path> OpenFile(std::span<const Modules::OpenFilter>) override {
        ++asked;
        return answer;
    }
    std::optional<std::filesystem::path> OpenFolder() override { return std::nullopt; }
    std::optional<std::filesystem::path> SaveFile(std::string_view) override { return std::nullopt; }
};

struct VetoClose final : Shell::LifecycleDelegate {
    int asked = 0;
    int started = 0;
    bool allow = false;
    bool WillClose(Shell::Shell&) override {
        ++asked;
        return allow;
    }
    void DidStart(Shell::Shell&) override { ++started; }
};

}

TEST_CASE("The shell opens a document, shows a viewer tab and records the recent") {
    TempDir dir("shell_open");
    const auto obx = WriteObx(dir.path / "fixture.obx");
    Shell::Shell shell(TestOptions(dir.path));
    REQUIRE(shell.Workspace().AddModule(std::make_unique<OnyxBox::Module>()).ok());
    REQUIRE(shell.Open(obx).ok());

    REQUIRE(StepUntil(shell, [&] { return !shell.Workspace().Documents().empty() && shell.Workspace().Documents()[0]->ready.load(); }));
    for (int i = 0; i < 10; ++i) {
        auto stepped = shell.Step();
        REQUIRE_MESSAGE(stepped.ok(), stepped.error().code << ": " << stepped.error().message);
        CHECK(stepped.value());
    }
    RequireWindowValidationLayer(*shell.GpuDevice());
    CHECK(shell.Workspace().Documents().size() == 1);
    REQUIRE(shell.RecentFiles().Entries().size() == 1);
    CHECK(shell.RecentFiles().Entries()[0].moduleId == "obx");

    auto* browser = static_cast<Ui::DocumentBrowser*>(shell.Panels().Find("onyx.browser"));
    auto* tabs = static_cast<Ui::DocumentTabs*>(shell.Panels().Find("onyx.documents"));
    REQUIRE(browser != nullptr);
    REQUIRE(tabs != nullptr);
    REQUIRE(static_cast<bool>(browser->onOpen));

    Ui::UiContext ui = shell.Context();
    const Ui::Selection image{shell.Workspace().Documents()[0]->id, Modules::NodePath{{0}}};
    browser->Select(ui, image);
    browser->onOpen(ui, image);
    CHECK(StepUntil(shell, [&] { return tabs->Count() == 1; }));
    for (int i = 0; i < 5; ++i) {
        REQUIRE(shell.Step().ok());
    }
    CHECK(tabs->Count() == 1);

    shell.GpuDevice()->GraphicsQueue().WaitIdle();
    CHECK_MESSAGE(shell.GpuDevice()->ValidationMessageCount() == 0, shell.GpuDevice()->LastValidationMessage());
}

TEST_CASE("A file routed from the command line opens in the window") {
    TempDir dir("shell_route_open");
    const auto obx = WriteObx(dir.path / "fixture.obx");
    Shell::Shell shell(TestOptions(dir.path));
    REQUIRE(shell.Workspace().AddModule(std::make_unique<OnyxBox::Module>()).ok());

    std::string program = "onyx";
    std::string argument = obx.string();
    char* argv[] = {program.data(), argument.data()};
    const Shell::ArgsRoute route = Shell::RouteArgs(2, argv, shell.Verbs());
    REQUIRE(route.kind == Shell::ArgsRoute::Kind::GuiOpen);
    REQUIRE(shell.Open(route.file).ok());

    REQUIRE(StepUntil(shell, [&] { return !shell.Workspace().Documents().empty() && shell.Workspace().Documents()[0]->ready.load(); }));
    RequireWindowValidationLayer(*shell.GpuDevice());
    CHECK(shell.Workspace().Documents().size() == 1);
    CHECK(shell.RecentFiles().Entries().size() == 1);

    shell.GpuDevice()->GraphicsQueue().WaitIdle();
    CHECK_MESSAGE(shell.GpuDevice()->ValidationMessageCount() == 0, shell.GpuDevice()->LastValidationMessage());
}

TEST_CASE("A theme font change rebuilds the atlas with the new font and size") {
    TempDir dir("shell_fonts");
    Shell::Shell shell(TestOptions(dir.path));
    REQUIRE(StepUntil(shell, [] { return true; }));
    RequireWindowValidationLayer(*shell.GpuDevice());
    ImGuiIO& io = ImGui::GetIO();
    REQUIRE(io.FontDefault != nullptr);
    const float sizeBefore = io.FontDefault->LegacySize;

    const std::filesystem::path font = std::filesystem::path(ONYX_SOURCE_DIR) / "third_party" / "fonts" / "SFMono-Regular.otf";
    shell.Theme().Mutate([&](Ui::ThemeState& state) {
        state.fontSizePt = 20.0f;
        state.fontPath = font.string();
    });
    REQUIRE(StepUntil(shell, [] { return true; }));
    REQUIRE(io.FontDefault != nullptr);
    CHECK(io.FontDefault->LegacySize != sizeBefore);
    CHECK(io.FontDefault->LegacySize == shell.Theme().Applied().atlasRefPx);
    CHECK(shell.Theme().Applied().atlasFontPath == font.string());
    REQUIRE(StepUntil(shell, [] { return true; }));
    shell.GpuDevice()->GraphicsQueue().WaitIdle();
    CHECK_MESSAGE(shell.GpuDevice()->ValidationMessageCount() == 0, shell.GpuDevice()->LastValidationMessage());
}

TEST_CASE("The ImGui ini lives under a config directory with a non-ASCII name") {
    TempDir dir("shell_ini");
    Shell::ShellOptions options = TestOptions(dir.path);
    options.configDir = dir.path / std::filesystem::path(std::u8string(u8"café"));
    Shell::Shell shell(options);
    REQUIRE(StepUntil(shell, [] { return true; }, 2));
    ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
    CHECK(std::filesystem::exists(options.configDir / "imgui.ini"));
}

TEST_CASE("A lifecycle delegate can veto the close") {
    TempDir dir("shell_veto");
    Shell::Shell shell(TestOptions(dir.path));
    auto delegate = std::make_unique<VetoClose>();
    VetoClose* veto = delegate.get();
    shell.SetLifecycle(std::move(delegate));

    REQUIRE(StepUntil(shell, [] { return true; }));
    CHECK(veto->started == 1);

    shell.RequestClose();
    auto stepped = shell.Step();
    REQUIRE(stepped.ok());
    CHECK(stepped.value());
    CHECK(veto->asked == 1);

    veto->allow = true;
    shell.RequestClose();
    stepped = shell.Step();
    REQUIRE(stepped.ok());
    CHECK_FALSE(stepped.value());
    CHECK(veto->asked == 2);
    shell.GpuDevice()->GraphicsQueue().WaitIdle();
    CHECK_MESSAGE(shell.GpuDevice()->ValidationMessageCount() == 0, shell.GpuDevice()->LastValidationMessage());
}

TEST_CASE("The open command runs its dialog between frames and opens the answer") {
    TempDir dir("shell_dialog");
    const auto obx = WriteObx(dir.path / "fixture.obx");
    Shell::Shell shell(TestOptions(dir.path));
    REQUIRE(shell.Workspace().AddModule(std::make_unique<OnyxBox::Module>()).ok());
    auto dialogs = std::make_unique<FixedDialogs>();
    FixedDialogs* fake = dialogs.get();
    fake->answer = obx;
    shell.SetDialogs(std::move(dialogs));

    REQUIRE(StepUntil(shell, [] { return true; }));
    Ui::UiContext ui = shell.Context();
    REQUIRE(shell.Commands().Run("onyx.file.open", ui).ok());
    CHECK(fake->asked == 0);
    REQUIRE(shell.Step().ok());
    CHECK(fake->asked == 1);
    CHECK(shell.Workspace().Documents().size() == 1);
    RequireWindowValidationLayer(*shell.GpuDevice());
    shell.GpuDevice()->GraphicsQueue().WaitIdle();
    CHECK_MESSAGE(shell.GpuDevice()->ValidationMessageCount() == 0, shell.GpuDevice()->LastValidationMessage());
}

TEST_CASE("Settings are written when the shell closes") {
    TempDir dir("shell_save");
    {
        Shell::Shell shell(TestOptions(dir.path));
        REQUIRE(StepUntil(shell, [] { return true; }));
        shell.Settings().Set("shell.probe", "kept");
        shell.RequestClose();
        auto stepped = shell.Step();
        REQUIRE(stepped.ok());
        CHECK_FALSE(stepped.value());
        RequireWindowValidationLayer(*shell.GpuDevice());
        shell.GpuDevice()->GraphicsQueue().WaitIdle();
        CHECK_MESSAGE(shell.GpuDevice()->ValidationMessageCount() == 0, shell.GpuDevice()->LastValidationMessage());
    }
    auto reloaded = Services::Settings::Load(dir.path / "config" / "settings.toml");
    CHECK(reloaded.GetString("shell.probe") == std::optional<std::string>("kept"));
}
