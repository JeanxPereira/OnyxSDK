#pragma once

#include <Onyx/Ui/Panel.h>
#include <Prism/Palette.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace Prism::Ui {

// One swatch per palette colour, with its index and #rrggbbaa underneath; hovering shows the RGBA.
class PaletteView final : public Onyx::Ui::IDocumentView {
public:
    PaletteView(Onyx::Modules::DocumentId doc, Onyx::Modules::NodePath node, std::string name,
                std::shared_ptr<const Prism::Palette> palette);

    std::string_view Id() const override { return m_id; }
    std::string_view Title() const override { return m_name; }
    void Draw(Onyx::Ui::UiContext& ui) override;
    Onyx::Modules::DocumentId Document() const override { return m_doc; }
    Onyx::Modules::NodePath Node() const override { return m_node; }

    size_t Count() const { return m_palette ? m_palette->colors.size() : 0; }

private:
    Onyx::Modules::DocumentId m_doc;
    Onyx::Modules::NodePath m_node;
    std::string m_name;
    std::string m_id;
    std::shared_ptr<const Prism::Palette> m_palette;
};

}
