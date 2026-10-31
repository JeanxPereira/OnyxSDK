#pragma once

#include <Onyx/Ui/UiContext.h>

#include <cstddef>

// A UiContext whose members are never dereferenced: the tests that use it exercise registries that
// only forward the context to callbacks.
struct BareContext {
    Onyx::Modules::Workspace workspace;
    alignas(std::max_align_t) std::byte storage[64]{};

    template <class T>
    T& Fake() { return *reinterpret_cast<T*>(storage); }

    Onyx::Ui::UiContext Make() {
        return Onyx::Ui::UiContext{workspace,
                                   nullptr,
                                   nullptr,
                                   Fake<Onyx::Ui::TexturePool>(),
                                   Fake<Onyx::Ui::Theme>(),
                                   Fake<Onyx::Ui::CommandRegistry>(),
                                   Fake<Onyx::Ui::ViewerRegistry>(),
                                   Fake<Onyx::Ui::PanelHost>(),
                                   Fake<Onyx::Ui::FrameClock>(),
                                   0.0f};
    }
};
