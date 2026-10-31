#pragma once

#include <Onyx/Ui/Command.h>
#include <Onyx/Ui/FrameClock.h>
#include <Onyx/Ui/PanelHost.h>
#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/ViewerRegistry.h>

#include <cstddef>

// A UiContext for tests that only forward the context to callbacks. Theme has no definition yet, so
// its reference points into raw storage that is never read; the controller replaces the fake with
// a real Theme member once the parallel task lands. Every other member is real.
struct BareContext {
    Onyx::Modules::Workspace workspace;
    Onyx::Ui::CommandRegistry commands;
    Onyx::Ui::ViewerRegistry viewers;
    Onyx::Ui::PanelHost panels;
    Onyx::Ui::FrameClock clock;
    alignas(std::max_align_t) std::byte themeStorage[64]{};

    Onyx::Ui::UiContext Make() {
        return Onyx::Ui::UiContext{workspace,
                                   nullptr,
                                   nullptr,
                                   nullptr,
                                   *reinterpret_cast<Onyx::Ui::Theme*>(themeStorage),
                                   commands,
                                   viewers,
                                   panels,
                                   clock,
                                   0.0f};
    }
};
