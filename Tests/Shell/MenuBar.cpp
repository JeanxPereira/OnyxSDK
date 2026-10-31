#include <doctest/doctest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "HeadlessImGui.h"
#include "Onyx/Shell/MenuBar.h"
#include "Onyx/Ui/Command.h"
#include "Onyx/Ui/PanelHost.h"

using namespace Onyx;
using Onyx::Shell::MenuNode;

namespace {

Ui::Command MakeCommand(std::string id, std::string path, std::optional<Ui::Shortcut> shortcut = std::nullopt) {
    Ui::Command command;
    command.id = std::move(id);
    command.menuPath = std::move(path);
    command.shortcut = shortcut;
    command.run = [](Ui::UiContext&) {};
    return command;
}

const MenuNode* FindChild(const MenuNode& node, const std::string& text) {
    for (const MenuNode& child : node.children) {
        if (child.text == text) return &child;
    }
    return nullptr;
}

class StubPanel final : public Ui::IPanel {
public:
    StubPanel(std::string id, std::string title) : m_id(std::move(id)), m_title(std::move(title)) {}
    std::string_view Id() const override { return m_id; }
    std::string_view Title() const override { return m_title; }
    void Draw(Ui::UiContext&) override {}

private:
    std::string m_id;
    std::string m_title;
};

}

TEST_CASE("MenuModel groups commands by menu path in registration order") {
    HeadlessImGui rig;
    Ui::CommandRegistry commands;
    commands.Add(MakeCommand("a.open", "File/Open\xE2\x80\xA6", Ui::Shortcut{ImGuiMod_Ctrl | ImGuiKey_O}));
    commands.Add(MakeCommand("a.hidden", ""));
    commands.Add(MakeCommand("a.recent", "File/Open Recent/one.obx"));
    commands.Add(MakeCommand("a.exit", "File/Exit"));
    commands.Add(MakeCommand("b.reset", "View/Reset layout"));
    Ui::PanelHost panels;
    panels.Add(std::make_unique<StubPanel>("p.one", "One"));
    panels.Add(std::make_unique<StubPanel>("p.two", "Two"));

    const std::vector<MenuNode> model = Shell::MenuModel(commands, panels);
    REQUIRE(model.size() == 2);
    CHECK(model[0].text == "File");
    CHECK(model[1].text == "View");

    const MenuNode& file = model[0];
    REQUIRE(file.children.size() == 3);
    CHECK(file.children[0].text == "Open\xE2\x80\xA6");
    CHECK(file.children[0].commandId == "a.open");
    CHECK(file.children[0].shortcut == "Ctrl+O");
    CHECK(file.children[1].text == "Open Recent");
    REQUIRE(file.children[1].children.size() == 1);
    CHECK(file.children[1].children[0].commandId == "a.recent");
    CHECK(file.children[2].text == "Exit");

    const MenuNode& view = model[1];
    CHECK(view.children.front().commandId == "b.reset");
    std::vector<std::string> toggles;
    for (const MenuNode& node : view.children) {
        if (node.kind == MenuNode::Kind::PanelToggle) toggles.push_back(node.panelId);
    }
    CHECK(toggles == std::vector<std::string>{"p.one", "p.two"});
    CHECK(FindChild(view, "One") != nullptr);
}

TEST_CASE("MenuModel adds a View menu when only panels exist") {
    Ui::CommandRegistry commands;
    Ui::PanelHost panels;
    panels.Add(std::make_unique<StubPanel>("p.one", "One"));
    const std::vector<MenuNode> model = Shell::MenuModel(commands, panels);
    REQUIRE(model.size() == 1);
    CHECK(model[0].text == "View");
    REQUIRE(model[0].children.size() == 1);
    CHECK(model[0].children[0].kind == MenuNode::Kind::PanelToggle);
}
