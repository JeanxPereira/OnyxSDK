#pragma once

#include <Onyx/Core/Services/Settings.h>
#include <Onyx/Ui/Command.h>
#include <Onyx/Ui/FrameClock.h>
#include <Onyx/Ui/PanelHost.h>
#include <Onyx/Ui/Theme.h>
#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/ViewerRegistry.h>

// A UiContext for tests that only forward the context to callbacks: no GPU, no texture pool, a
// theme over empty settings, and real registries.
struct BareContext {
    Onyx::Modules::Workspace workspace;
    Onyx::Services::Settings settings = Onyx::Services::Settings::Empty();
    Onyx::Ui::Theme theme{settings};
    Onyx::Ui::CommandRegistry commands;
    Onyx::Ui::ViewerRegistry viewers;
    Onyx::Ui::PanelHost panels;
    Onyx::Ui::FrameClock clock;

    Onyx::Ui::UiContext Make() {
        return Onyx::Ui::UiContext{workspace, nullptr, nullptr, nullptr, theme,
                                   commands,  viewers, panels,  clock,   0.0f};
    }
};
