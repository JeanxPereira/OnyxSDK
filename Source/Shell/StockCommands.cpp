#include "Onyx/Shell/StockCommands.h"

#include <imgui.h>

#include <string>

#include "Onyx/Core/Services/Log.h"
#include "Onyx/Shell/Shell.h"
#include "Onyx/Ui/Viewers/DocumentTabs.h"
#include "Utf8.h"

namespace Onyx::Shell {

namespace {

constexpr const char* kEllipsis = "\xE2\x80\xA6";

Ui::Command Make(std::string id, std::string label, std::string menuPath, std::optional<Ui::Shortcut> shortcut,
                 std::function<void(Ui::UiContext&)> run) {
    Ui::Command command;
    command.id = std::move(id);
    command.label = std::move(label);
    command.menuPath = std::move(menuPath);
    command.shortcut = shortcut;
    command.run = std::move(run);
    return command;
}

void AddOnce(Ui::CommandRegistry& commands, Ui::Command command) {
    if (!commands.Find(command.id)) {
        commands.Add(std::move(command));
    }
}

Ui::DocumentTabs* Tabs(Shell& shell) {
    return dynamic_cast<Ui::DocumentTabs*>(shell.Panels().Find("onyx.documents"));
}

void SyncRecentCommands(Ui::CommandRegistry& commands, Shell& shell) {
    const auto entries = shell.RecentFiles().Entries();
    for (size_t i = 0; i < kMaxRecents; ++i) {
        const std::string id = "onyx.file.openRecent." + std::to_string(i);
        if (i >= entries.size()) {
            commands.Remove(id);
            continue;
        }
        const RecentEntry entry = entries[i];
        commands.Add(Make(id, entry.displayName, "File/Open Recent/" + entry.displayName, std::nullopt,
                          [&shell, entry](Ui::UiContext&) {
                              auto opened = shell.Open(entry.path, entry.moduleId);
                              if (!opened.ok()) {
                                  Services::Log::Default().Write(Services::LogLevel::Warn, "shell.open", "{}: {}",
                                                                 opened.error().code, opened.error().message);
                              }
                          }));
    }
}

void SyncPanelToggles(Ui::CommandRegistry& commands, Shell& shell) {
    for (const auto& panel : shell.Panels().All()) {
        const std::string panelId(panel->Id());
        commands.Add(Make("onyx.view.toggle." + panelId, "Toggle " + std::string(panel->Title()), "", std::nullopt,
                          [&shell, panelId](Ui::UiContext&) {
                              if (Ui::IPanel* target = shell.Panels().Find(panelId)) {
                                  target->visible = !target->visible;
                              }
                          }));
    }
}

}

void AddStockCommands(Ui::CommandRegistry& commands, Shell& shell) {
    const auto chord = [](ImGuiKeyChord keys) { return std::optional<Ui::Shortcut>(Ui::Shortcut{keys}); };

    AddOnce(commands, Make("onyx.file.open", "Open", std::string("File/Open") + kEllipsis,
                           chord(ImGuiMod_Ctrl | ImGuiKey_O), [&shell](Ui::UiContext&) { shell.RequestOpenDialog(); }));
    AddOnce(commands, Make("onyx.file.closeActive", "Close", "File/Close", chord(ImGuiMod_Ctrl | ImGuiKey_W),
                           [&shell](Ui::UiContext&) {
                               if (Ui::DocumentTabs* tabs = Tabs(shell)) {
                                   tabs->CloseActive();
                               }
                           }));
    AddOnce(commands, Make("onyx.file.closeAll", "Close all", "File/Close all", std::nullopt,
                           [&shell](Ui::UiContext&) {
                               if (Ui::DocumentTabs* tabs = Tabs(shell)) {
                                   tabs->CloseAll();
                               }
                           }));
    AddOnce(commands, Make("onyx.file.exit", "Exit", "File/Exit", chord(ImGuiMod_Alt | ImGuiKey_F4),
                           [&shell](Ui::UiContext&) { shell.RequestClose(); }));
    AddOnce(commands, Make("onyx.view.resetLayout", "Reset layout", "View/Reset layout", std::nullopt,
                           [&shell](Ui::UiContext&) { shell.RequestResetLayout(); }));
    AddOnce(commands, Make("onyx.view.settings", "Settings", std::string("View/Settings") + kEllipsis,
                           chord(ImGuiMod_Ctrl | ImGuiKey_Comma), [&shell](Ui::UiContext&) {
                               if (Ui::IPanel* panel = shell.Panels().Find("onyx.settings")) {
                                   panel->visible = true;
                               }
                           }));

    SyncRecentCommands(commands, shell);
    SyncPanelToggles(commands, shell);
}

}
