#include "Onyx/Shell/MenuBar.h"

#include <imgui.h>

#include <string_view>

#include "Onyx/Ui/Shortcut.h"

namespace Onyx::Shell {

namespace {

constexpr const char* kViewMenu = "View";

std::vector<std::string_view> Split(std::string_view path) {
    std::vector<std::string_view> parts;
    while (true) {
        const size_t slash = path.find('/');
        parts.push_back(path.substr(0, slash));
        if (slash == std::string_view::npos) {
            break;
        }
        path.remove_prefix(slash + 1);
    }
    return parts;
}

MenuNode* FindMenu(std::vector<MenuNode>& nodes, std::string_view text) {
    for (MenuNode& node : nodes) {
        if (node.kind == MenuNode::Kind::Menu && node.text == text) {
            return &node;
        }
    }
    return nullptr;
}

MenuNode& EnsureMenu(std::vector<MenuNode>& nodes, std::string_view text) {
    if (MenuNode* existing = FindMenu(nodes, text)) {
        return *existing;
    }
    MenuNode node;
    node.kind = MenuNode::Kind::Menu;
    node.text = std::string(text);
    nodes.push_back(std::move(node));
    return nodes.back();
}

struct Clicked {
    std::string commandId;
};

void DrawNode(Ui::UiContext& ui, const MenuNode& node, Clicked& clicked) {
    switch (node.kind) {
    case MenuNode::Kind::Separator:
        ImGui::Separator();
        break;
    case MenuNode::Kind::Menu:
        if (ImGui::BeginMenu(node.text.c_str())) {
            for (const MenuNode& child : node.children) {
                DrawNode(ui, child, clicked);
            }
            ImGui::EndMenu();
        }
        break;
    case MenuNode::Kind::PanelToggle:
        if (Ui::IPanel* panel = ui.panels.Find(node.panelId)) {
            ImGui::PushID(node.panelId.c_str());
            ImGui::MenuItem(node.text.c_str(), nullptr, &panel->visible);
            ImGui::PopID();
        }
        break;
    case MenuNode::Kind::Command: {
        const Ui::Command* command = ui.commands.Find(node.commandId);
        const bool enabled = command && (!command->enabled || command->enabled(ui));
        ImGui::PushID(node.commandId.c_str());
        if (ImGui::MenuItem(node.text.c_str(), node.shortcut.empty() ? nullptr : node.shortcut.c_str(), false,
                            enabled)) {
            clicked.commandId = node.commandId;
        }
        ImGui::PopID();
        break;
    }
    }
}

}

std::vector<MenuNode> MenuModel(const Ui::CommandRegistry& commands, const Ui::PanelHost& panels) {
    std::vector<MenuNode> roots;
    for (const Ui::Command& command : commands.All()) {
        if (command.menuPath.empty()) {
            continue;
        }
        const std::vector<std::string_view> parts = Split(command.menuPath);
        std::vector<MenuNode>* level = &roots;
        for (size_t i = 0; i + 1 < parts.size(); ++i) {
            level = &EnsureMenu(*level, parts[i]).children;
        }
        MenuNode item;
        item.kind = MenuNode::Kind::Command;
        item.text = std::string(parts.back());
        item.commandId = command.id;
        if (command.shortcut) {
            item.shortcut = Ui::FormatShortcut(*command.shortcut);
        }
        level->push_back(std::move(item));
    }

    if (!panels.All().empty()) {
        MenuNode& view = EnsureMenu(roots, kViewMenu);
        if (!view.children.empty()) {
            MenuNode separator;
            separator.kind = MenuNode::Kind::Separator;
            view.children.push_back(std::move(separator));
        }
        for (const auto& panel : panels.All()) {
            MenuNode toggle;
            toggle.kind = MenuNode::Kind::PanelToggle;
            toggle.text = std::string(panel->Title());
            toggle.panelId = std::string(panel->Id());
            view.children.push_back(std::move(toggle));
        }
    }
    return roots;
}

TitleBarRect DrawMenuBar(Ui::UiContext& ui) {
    TitleBarRect bar;
    Clicked clicked;
    const std::vector<MenuNode> model = MenuModel(ui.commands, ui.panels);
    if (ImGui::BeginMainMenuBar()) {
        const ImVec2 position = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        bar.x = position.x;
        bar.y = position.y;
        bar.width = size.x;
        bar.height = size.y;
        for (const MenuNode& node : model) {
            DrawNode(ui, node, clicked);
        }
        ImGui::EndMainMenuBar();
    }
    if (!clicked.commandId.empty()) {
        auto ran = ui.commands.Run(clicked.commandId, ui);
        (void)ran;
    }
    return bar;
}

}
