#pragma once

#include <Onyx/Core/Result.h>
#include <Onyx/Ui/Shortcut.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Onyx::Ui {

struct UiContext;

struct Command {
    std::string id;
    std::string label;
    std::string menuPath;
    std::optional<Shortcut> shortcut;
    std::function<bool(UiContext&)> enabled;
    std::function<void(UiContext&)> run;
};

class CommandRegistry {
public:
    // Replaces a command with the same id, keeping its position. A command with an empty id is ignored.
    void Add(Command command);
    void Remove(std::string_view id);

    // Replaces every field except the id. Fails with "ui.command.unknown" when the id is absent.
    Result<void> Override(std::string_view id, Command command);

    // Fails with "ui.command.unknown" for an unknown id; does nothing, successfully, when `enabled`
    // says no or `run` is empty.
    Result<void> Run(std::string_view id, UiContext& ui);

    const std::vector<Command>& All() const { return m_commands; }
    const Command* Find(std::string_view id) const;

    // Runs the first enabled command whose shortcut is pressed this frame.
    void PollShortcuts(UiContext& ui);

private:
    std::vector<Command> m_commands;
};

}
