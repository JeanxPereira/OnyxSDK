#include "Onyx/Ui/Command.h"

namespace Onyx::Ui {

namespace {

Error Unknown(std::string_view id) {
    return Error{"ui.command.unknown", "No command with id '" + std::string(id) + "'"};
}

}

void CommandRegistry::Add(Command command) {
    for (Command& existing : m_commands) {
        if (existing.id == command.id) {
            existing = std::move(command);
            return;
        }
    }
    m_commands.push_back(std::move(command));
}

void CommandRegistry::Remove(std::string_view id) {
    std::erase_if(m_commands, [&](const Command& c) { return c.id == id; });
}

Result<void> CommandRegistry::Override(std::string_view id, Command command) {
    for (Command& existing : m_commands) {
        if (existing.id == id) {
            command.id = existing.id;
            existing = std::move(command);
            return Result<void>::Ok();
        }
    }
    return Result<void>::Fail(Unknown(id));
}

Result<void> CommandRegistry::Run(std::string_view id, UiContext& ui) {
    const Command* found = Find(id);
    if (!found) return Result<void>::Fail(Unknown(id));
    // A command may edit the registry while it runs, so work on copies of its callables.
    const auto enabled = found->enabled;
    const auto run = found->run;
    if (enabled && !enabled(ui)) return Result<void>::Ok();
    if (run) run(ui);
    return Result<void>::Ok();
}

const Command* CommandRegistry::Find(std::string_view id) const {
    for (const Command& c : m_commands) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

void CommandRegistry::PollShortcuts(UiContext& ui) {
    for (size_t i = 0; i < m_commands.size(); ++i) {
        const Command& c = m_commands[i];
        if (!c.shortcut || !ShortcutPressed(*c.shortcut)) continue;
        if (c.enabled && !c.enabled(ui)) continue;
        const auto run = c.run;
        if (run) run(ui);
        return;
    }
}

}
