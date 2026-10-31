#include <PrismUi/Extension.h>

#include <PrismUi/PaletteView.h>

#include <memory>
#include <utility>

namespace Prism::Ui {

void Extension::Install(Onyx::Shell::Shell& shell) {
    Onyx::Ui::ViewerSpec spec;
    spec.id = "prism.palette";
    spec.target = Onyx::Types::TypeKey{"prm.palette"};
    spec.priority = 10;
    spec.anyCapability = "prism.palette";
    spec.create = [](Onyx::Ui::OpenRequest& r) -> std::unique_ptr<Onyx::Ui::IDocumentView> {
        auto palette = std::static_pointer_cast<const Prism::Palette>(r.any);
        if (!palette) return nullptr;
        return std::make_unique<PaletteView>(r.doc, std::move(r.node), std::move(r.name), std::move(palette));
    };
    shell.Viewers().Register(std::move(spec));
}

}
