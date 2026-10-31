#pragma once

#include <Onyx/Shell/Shell.h>

namespace Prism::Ui {

// Registers the palette viewer over the "prism.palette" any-capability.
class Extension final : public Onyx::Shell::IShellExtension {
public:
    void Install(Onyx::Shell::Shell& shell) override;
};

}
